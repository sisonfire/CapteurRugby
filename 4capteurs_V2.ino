#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <VL53L1X.h>

#define TCA_ADDR 0x70
#define NB_CAPTEURS 4

VL53L1X sensor[NB_CAPTEURS];
WebServer server(80);
Preferences prefs;

// ===== Réseau WiFi (à adapter) =====
const char *WIFI_SSID = "CapteurRugby";
const char *WIFI_PASS = "ChangeMoi123";

// ===== Réglages globaux détection =====
const int DISTANCE_INVALID = -1;
const int MAX_SENSOR_MM = 4000;
const int SEUIL_CHUTE_MM = 420;      // chute mini entre distance à vide et distance lue
const int CONFIRMATION = 2;          // nb de lectures consécutives pour valider
const int LIBERATION = 1;            // nb de lectures sans détection pour relâcher
const uint32_t DETECTION_COOLDOWN_MS = 220; // évite les doubles triggers

// ===== Réglages mesure =====
const int MEDIAN_WINDOW = 5;
const float EMA_ALPHA = 0.42f;       // compromis stabilité / réactivité

struct SensorConfig {
  int minMM;
  int maxMM;
};

struct SensorRuntime {
  int distanceRaw;
  int distanceMedian;
  int distanceFiltered;
  int distanceVide;
  int chute;
  bool ok;
  int detectCount;
  int releaseCount;
  bool detect;
  int hist[MEDIAN_WINDOW];
  int histCount;
  int histPos;
  bool emaInit;
  float ema;
};

SensorConfig cfg[NB_CAPTEURS];
SensorRuntime rt[NB_CAPTEURS];

bool ballonEtat = false;
int capteurActif = -1;
int distanceActive = DISTANCE_INVALID;
uint32_t lastDetectionMs = 0;

void tcaSelect(uint8_t channel) {
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(1 << channel);
  Wire.endTransmission();
}

int clampDistance(int d) {
  if (d <= 0 || d > MAX_SENSOR_MM) return DISTANCE_INVALID;
  return d;
}

int lireDistanceRaw(int i) {
  tcaSelect(i);
  delay(2);
  int d = sensor[i].read();
  if (sensor[i].timeoutOccurred()) return DISTANCE_INVALID;
  return clampDistance(d);
}

int medianFromHistory(const SensorRuntime &s) {
  if (s.histCount == 0) return DISTANCE_INVALID;
  int tmp[MEDIAN_WINDOW];
  for (int i = 0; i < s.histCount; i++) tmp[i] = s.hist[i];

  for (int i = 1; i < s.histCount; i++) {
    int key = tmp[i];
    int j = i - 1;
    while (j >= 0 && tmp[j] > key) {
      tmp[j + 1] = tmp[j];
      j--;
    }
    tmp[j + 1] = key;
  }

  return tmp[s.histCount / 2];
}

void updateFilter(int idx, int raw) {
  SensorRuntime &s = rt[idx];
  s.distanceRaw = raw;

  if (raw == DISTANCE_INVALID) {
    return;
  }

  s.hist[s.histPos] = raw;
  s.histPos = (s.histPos + 1) % MEDIAN_WINDOW;
  if (s.histCount < MEDIAN_WINDOW) s.histCount++;

  s.distanceMedian = medianFromHistory(s);

  if (!s.emaInit) {
    s.ema = s.distanceMedian;
    s.emaInit = true;
  } else {
    s.ema = EMA_ALPHA * s.distanceMedian + (1.0f - EMA_ALPHA) * s.ema;
  }
  s.distanceFiltered = (int)(s.ema + 0.5f);
}

void defaultsConfig() {
  for (int i = 0; i < NB_CAPTEURS; i++) {
    cfg[i].minMM = 120;
    cfg[i].maxMM = 1400;
  }
}

void loadConfig() {
  defaultsConfig();
  prefs.begin("capteurs", true);
  for (int i = 0; i < NB_CAPTEURS; i++) {
    String kMin = "c" + String(i) + "_min";
    String kMax = "c" + String(i) + "_max";
    cfg[i].minMM = prefs.getInt(kMin.c_str(), cfg[i].minMM);
    cfg[i].maxMM = prefs.getInt(kMax.c_str(), cfg[i].maxMM);
  }
  prefs.end();
}

void saveConfig() {
  prefs.begin("capteurs", false);
  for (int i = 0; i < NB_CAPTEURS; i++) {
    String kMin = "c" + String(i) + "_min";
    String kMax = "c" + String(i) + "_max";
    prefs.putInt(kMin.c_str(), cfg[i].minMM);
    prefs.putInt(kMax.c_str(), cfg[i].maxMM);
  }
  prefs.end();
}

String toJsonStatus() {
  String json = "{";
  json += "\"ballon\":" + String(ballonEtat ? "true" : "false") + ",";
  json += "\"capteurActif\":" + String(capteurActif) + ",";
  json += "\"distanceActive\":" + String(distanceActive) + ",";
  json += "\"capteurs\":[";

  for (int i = 0; i < NB_CAPTEURS; i++) {
    if (i) json += ",";
    json += "{";
    json += "\"id\":" + String(i) + ",";
    json += "\"ok\":" + String(rt[i].ok ? "true" : "false") + ",";
    json += "\"raw\":" + String(rt[i].distanceRaw) + ",";
    json += "\"filtre\":" + String(rt[i].distanceFiltered) + ",";
    json += "\"vide\":" + String(rt[i].distanceVide) + ",";
    json += "\"chute\":" + String(rt[i].chute) + ",";
    json += "\"detect\":" + String(rt[i].detect ? "true" : "false") + ",";
    json += "\"min\":" + String(cfg[i].minMM) + ",";
    json += "\"max\":" + String(cfg[i].maxMM);
    json += "}";
  }
  json += "]}";
  return json;
}

void handleRoot() {
  String html = R"HTML(
<!doctype html><html lang='fr'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Capteurs Rugby V2</title>
<style>body{font-family:Arial;margin:20px;background:#0c1222;color:#fff}table{border-collapse:collapse;width:100%;max-width:920px}th,td{border:1px solid #2f3d66;padding:8px;text-align:center}input{width:90px}button{padding:8px 14px;margin:6px}.ok{color:#7CFC86}.ko{color:#ff7a7a}</style></head>
<body><h2>Capteurs Rugby V2</h2><div id='etat'>Chargement...</div>
<table><thead><tr><th>Capteur</th><th>OK</th><th>Raw</th><th>Filtré</th><th>Vide</th><th>Chute</th><th>Detect</th><th>Min(mm)</th><th>Max(mm)</th></tr></thead><tbody id='tb'></tbody></table>
<button onclick='save()'>Sauvegarder</button><span id='msg'></span>
<script>
async function refresh(){
 const r=await fetch('/api/status');const j=await r.json();
 document.getElementById('etat').innerText=`Ballon: ${j.ballon?'DETECTE':'absent'} | Capteur actif: ${j.capteurActif} | Distance: ${j.distanceActive} mm`;
 const tb=document.getElementById('tb');tb.innerHTML='';
 j.capteurs.forEach(c=>{const tr=document.createElement('tr');
 tr.innerHTML=`<td>C${c.id}</td><td class='${c.ok?'ok':'ko'}'>${c.ok?'OK':'KO'}</td><td>${c.raw}</td><td>${c.filtre}</td><td>${c.vide}</td><td>${c.chute}</td><td>${c.detect}</td><td><input id='min_${c.id}' type='number' value='${c.min}'></td><td><input id='max_${c.id}' type='number' value='${c.max}'></td>`;
 tb.appendChild(tr);
 });
}
async function save(){
 const payload={capteurs:[]};
 for(let i=0;i<4;i++){payload.capteurs.push({id:i,min:parseInt(document.getElementById(`min_${i}`).value),max:parseInt(document.getElementById(`max_${i}`).value)});}
 const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});
 document.getElementById('msg').innerText=r.ok?' ✅ sauvegardé':' ❌ erreur';
 setTimeout(()=>document.getElementById('msg').innerText='',2000);
}
setInterval(refresh,250);refresh();
</script></body></html>)HTML";
  server.send(200, "text/html", html);
}

void handleStatus() { server.send(200, "application/json", toJsonStatus()); }

void handleConfigPost() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "body manquant");
    return;
  }

  String body = server.arg("plain");
  for (int i = 0; i < NB_CAPTEURS; i++) {
    String m1 = "\"id\":" + String(i);
    int p = body.indexOf(m1);
    if (p < 0) continue;

    int pMin = body.indexOf("\"min\":", p);
    int pMax = body.indexOf("\"max\":", p);
    if (pMin > 0) cfg[i].minMM = body.substring(pMin + 6).toInt();
    if (pMax > 0) cfg[i].maxMM = body.substring(pMax + 6).toInt();

    if (cfg[i].minMM < 40) cfg[i].minMM = 40;
    if (cfg[i].maxMM > 3000) cfg[i].maxMM = 3000;
    if (cfg[i].maxMM <= cfg[i].minMM) cfg[i].maxMM = cfg[i].minMM + 50;
  }

  saveConfig();
  server.send(200, "text/plain", "ok");
}

void calibrerVide() {
  for (int i = 0; i < NB_CAPTEURS; i++) {
    if (!rt[i].ok) continue;

    long somme = 0;
    int nb = 0;
    for (int j = 0; j < 30; j++) {
      int d = lireDistanceRaw(i);
      if (d > 700 && d < 3500) {
        somme += d;
        nb++;
      }
      delay(35);
    }
    rt[i].distanceVide = (nb > 0) ? (somme / nb) : 2000;
  }
}

void setupWeb() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connexion WiFi");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(250);
    Serial.print(".");
    tries++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi non connecté, mode local uniquement");
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.begin();
}

void setupCapteurs() {
  Wire.begin();
  Wire.setClock(100000);

  for (int i = 0; i < NB_CAPTEURS; i++) {
    rt[i].ok = false;
    rt[i].distanceRaw = DISTANCE_INVALID;
    rt[i].distanceMedian = DISTANCE_INVALID;
    rt[i].distanceFiltered = DISTANCE_INVALID;
    rt[i].distanceVide = 2000;
    rt[i].detect = false;
    rt[i].histCount = 0;
    rt[i].histPos = 0;
    rt[i].emaInit = false;

    tcaSelect(i);
    sensor[i].setTimeout(100);
    if (sensor[i].init()) {
      sensor[i].setDistanceMode(VL53L1X::Long);
      sensor[i].setMeasurementTimingBudget(20000);
      sensor[i].startContinuous(25);
      rt[i].ok = true;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(800);

  loadConfig();
  setupCapteurs();
  calibrerVide();
  setupWeb();
}

void loop() {
  bool ballonDetecte = false;
  int capteurDetecte = -1;
  int distanceDetectee = DISTANCE_INVALID;

  for (int i = 0; i < NB_CAPTEURS; i++) {
    if (!rt[i].ok) continue;

    int raw = lireDistanceRaw(i);
    updateFilter(i, raw);

    if (rt[i].distanceFiltered == DISTANCE_INVALID) {
      rt[i].detectCount = 0;
      rt[i].releaseCount++;
      continue;
    }

    rt[i].chute = rt[i].distanceVide - rt[i].distanceFiltered;

    bool detectionCandidate =
      rt[i].distanceFiltered >= cfg[i].minMM &&
      rt[i].distanceFiltered <= cfg[i].maxMM &&
      rt[i].chute >= SEUIL_CHUTE_MM;

    if (detectionCandidate) {
      rt[i].detectCount++;
      rt[i].releaseCount = 0;
    } else {
      rt[i].releaseCount++;
      if (rt[i].releaseCount >= LIBERATION) {
        rt[i].detectCount = 0;
      }
    }

    rt[i].detect = (rt[i].detectCount >= CONFIRMATION);

    if (rt[i].detect) {
      ballonDetecte = true;
      capteurDetecte = i;
      distanceDetectee = rt[i].distanceFiltered;
    }
  }

  uint32_t nowMs = millis();
  bool cooldownDone = (nowMs - lastDetectionMs) > DETECTION_COOLDOWN_MS;

  if (ballonDetecte && !ballonEtat && cooldownDone) {
    Serial.print("BALLON | C");
    Serial.print(capteurDetecte);
    Serial.print(" | Distance ");
    Serial.print(distanceDetectee);
    Serial.println(" mm");

    ballonEtat = true;
    capteurActif = capteurDetecte;
    distanceActive = distanceDetectee;
    lastDetectionMs = nowMs;
  }

  if (!ballonDetecte && ballonEtat) {
    ballonEtat = false;
    capteurActif = -1;
    distanceActive = DISTANCE_INVALID;
  }

  server.handleClient();
  delay(8);
}

#include <Wire.h>
#include <VL53L1X.h>

#define TCA_ADDR 0x70
#define NB_CAPTEURS 4

VL53L1X sensor[NB_CAPTEURS];

// --- Réglages de détection ---
const int DIST_MIN_MM = 120;
const int DIST_MAX_MM = 1200;
const int SEUIL_CHUTE_MM = 420;
const int CONFIRMATION = 2;
const int LIBERATION = 2;
const uint32_t DETECTION_COOLDOWN_MS = 180;

// --- Réglages stabilité lecture ---
const int MEDIAN_WINDOW = 5;         // anti-pics
const float EMA_ALPHA = 0.45f;       // lissage réactif
const int MAX_INVALID_CONSEC = 6;    // limite les faux resets
const uint32_t SERIAL_STATUS_INTERVAL_MS = 100;  // trame PC 10 Hz
const float BASELINE_TRACK_ALPHA = 0.04f;  // suivi lent du "vide"

int distanceVide[NB_CAPTEURS] = {2000, 2000, 2000, 2000};
float distanceFiltree[NB_CAPTEURS] = {2000, 2000, 2000, 2000};
int compteurOn[NB_CAPTEURS] = {0, 0, 0, 0};
int compteurOff[NB_CAPTEURS] = {0, 0, 0, 0};
int invalidConsec[NB_CAPTEURS] = {0, 0, 0, 0};
int derniereBrute[NB_CAPTEURS] = {-1, -1, -1, -1};
int derniereStable[NB_CAPTEURS] = {-1, -1, -1, -1};
int derniereChute[NB_CAPTEURS] = {0, 0, 0, 0};
bool derniereDetection[NB_CAPTEURS] = {false, false, false, false};
bool capteurOK[NB_CAPTEURS] = {false, false, false, false};

int ring[NB_CAPTEURS][MEDIAN_WINDOW];
int ringPos[NB_CAPTEURS] = {0, 0, 0, 0};
int ringCount[NB_CAPTEURS] = {0, 0, 0, 0};

bool ballonEtat = false;
uint32_t dernierEventMs = 0;
uint32_t dernierStatusSerialMs = 0;

void tcaSelect(uint8_t channel) {
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(1 << channel);
  Wire.endTransmission();
}

int medianOf(int *vals, int n) {
  for (int i = 0; i < n - 1; i++) {
    for (int j = i + 1; j < n; j++) {
      if (vals[j] < vals[i]) {
        int t = vals[i];
        vals[i] = vals[j];
        vals[j] = t;
      }
    }
  }
  return vals[n / 2];
}

int lireDistanceBrute(int i) {
  tcaSelect(i);
  delay(2);

  int d = sensor[i].read();

  if (sensor[i].timeoutOccurred()) {
    derniereBrute[i] = -1;
    return -1;
  }
  if (d <= 0 || d > 4000) {
    derniereBrute[i] = -1;
    return -1;
  }

  derniereBrute[i] = d;
  return d;
}

bool lireDistanceStable(int i, int &stableOut) {
  int d = lireDistanceBrute(i);
  if (d < 0) {
    invalidConsec[i]++;
    return false;
  }

  invalidConsec[i] = 0;

  ring[i][ringPos[i]] = d;
  ringPos[i] = (ringPos[i] + 1) % MEDIAN_WINDOW;
  if (ringCount[i] < MEDIAN_WINDOW) ringCount[i]++;

  int temp[MEDIAN_WINDOW];
  for (int k = 0; k < ringCount[i]; k++) temp[k] = ring[i][k];
  int median = medianOf(temp, ringCount[i]);

  if (ringCount[i] == 1) {
    distanceFiltree[i] = median;
  } else {
    distanceFiltree[i] = EMA_ALPHA * median + (1.0f - EMA_ALPHA) * distanceFiltree[i];
  }

  stableOut = (int)(distanceFiltree[i] + 0.5f);
  derniereStable[i] = stableOut;
  return true;
}

void envoyerStatusSerial() {
  Serial.print("DATA,ms=");
  Serial.print(millis());

  for (int i = 0; i < NB_CAPTEURS; i++) {
    Serial.print(",C");
    Serial.print(i);
    Serial.print("_ok=");
    Serial.print(capteurOK[i] ? 1 : 0);
    Serial.print(",C");
    Serial.print(i);
    Serial.print("_raw=");
    Serial.print(derniereBrute[i]);
    Serial.print(",C");
    Serial.print(i);
    Serial.print("_stable=");
    Serial.print(derniereStable[i]);
    Serial.print(",C");
    Serial.print(i);
    Serial.print("_vide=");
    Serial.print(distanceVide[i]);
    Serial.print(",C");
    Serial.print(i);
    Serial.print("_chute=");
    Serial.print(derniereChute[i]);
    Serial.print(",C");
    Serial.print(i);
    Serial.print("_det=");
    Serial.print(derniereDetection[i] ? 1 : 0);
    Serial.print(",C");
    Serial.print(i);
    Serial.print("_invalid=");
    Serial.print(invalidConsec[i]);
  }

  Serial.print(",ballon=");
  Serial.println(ballonEtat ? 1 : 0);
}

void envoyerDetectionSerial(int capteur, int distance) {
  Serial.print("BALLON,ms=");
  Serial.print(millis());
  Serial.print(",capteur=C");
  Serial.print(capteur);
  Serial.print(",distance=");
  Serial.print(distance);
  Serial.println(",unite=mm");
}

void calibrerVide() {
  for (int i = 0; i < NB_CAPTEURS; i++) {
    if (!capteurOK[i]) continue;

    long somme = 0;
    int nb = 0;

    for (int j = 0; j < 25; j++) {
      int d = lireDistanceBrute(i);
      if (d > 900 && d < 3500) {
        somme += d;
        nb++;
      }
      delay(35);
    }

    int base = (nb > 0) ? (int)(somme / nb) : 2000;
    distanceVide[i] = base;
    distanceFiltree[i] = base;

    for (int k = 0; k < MEDIAN_WINDOW; k++) ring[i][k] = base;
    ringCount[i] = MEDIAN_WINDOW;
    ringPos[i] = 0;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin();
  Wire.setClock(100000);

  for (int i = 0; i < NB_CAPTEURS; i++) {
    tcaSelect(i);
    sensor[i].setTimeout(120);

    if (sensor[i].init()) {
      sensor[i].setDistanceMode(VL53L1X::Long);
      sensor[i].setMeasurementTimingBudget(50000);
      sensor[i].startContinuous(35);
      capteurOK[i] = true;
    }
  }

  calibrerVide();
  Serial.println("V6 ready");
  Serial.println("Format DATA: DATA,ms=...,C0_raw=...,C0_stable=...,C0_vide=...,C0_chute=...,C0_det=...,ballon=...");
  Serial.println("Format detection: BALLON,ms=...,capteur=Cx,distance=...,unite=mm");
}

void loop() {
  bool ballonDetecte = false;
  int capteurDetecte = -1;
  int distanceDetectee = -1;
  uint32_t now = millis();

  for (int i = 0; i < NB_CAPTEURS; i++) {
    if (!capteurOK[i]) continue;

    int d = -1;
    bool ok = lireDistanceStable(i, d);
    if (!ok) {
      if (invalidConsec[i] > MAX_INVALID_CONSEC) {
        compteurOn[i] = 0;
        compteurOff[i] = LIBERATION;
      }
      derniereDetection[i] = false;
      derniereStable[i] = -1;
      derniereChute[i] = 0;
      continue;
    }

    int chute = distanceVide[i] - d;
    bool detectionCapteur = (d >= DIST_MIN_MM && d <= DIST_MAX_MM && chute >= SEUIL_CHUTE_MM);
    derniereChute[i] = chute;
    derniereDetection[i] = detectionCapteur;

    if (detectionCapteur) {
      compteurOn[i]++;
      compteurOff[i] = 0;
    } else {
      compteurOn[i] = 0;
      if (compteurOff[i] < LIBERATION) compteurOff[i]++;

      // suivi lent du vide uniquement quand pas de détection
      float tracked = (1.0f - BASELINE_TRACK_ALPHA) * distanceVide[i] + BASELINE_TRACK_ALPHA * d;
      distanceVide[i] = (int)(tracked + 0.5f);
    }

    if (compteurOn[i] >= CONFIRMATION) {
      ballonDetecte = true;
      capteurDetecte = i;
      distanceDetectee = d;
    }
  }

  bool cooldownOk = (now - dernierEventMs) >= DETECTION_COOLDOWN_MS;

  if (ballonDetecte && !ballonEtat && cooldownOk) {
    envoyerDetectionSerial(capteurDetecte, distanceDetectee);

    ballonEtat = true;
    dernierEventMs = now;
  }

  if (!ballonDetecte && ballonEtat) {
    bool released = true;
    for (int i = 0; i < NB_CAPTEURS; i++) {
      if (capteurOK[i] && compteurOff[i] < LIBERATION) {
        released = false;
        break;
      }
    }

    if (released) {
      ballonEtat = false;
    }
  }

  if ((now - dernierStatusSerialMs) >= SERIAL_STATUS_INTERVAL_MS) {
    dernierStatusSerialMs = now;
    envoyerStatusSerial();
  }
}

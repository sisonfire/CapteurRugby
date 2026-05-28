#include <Wire.h>
#include <VL53L1X.h>

#define TCA_ADDR 0x70
#define NB_CAPTEURS 4

VL53L1X sensor[NB_CAPTEURS];

// --- V7: version légère / rapide ---
const int DIST_MIN_MM = 120;
const int DIST_MAX_MM = 1200;
const int SEUIL_CHUTE_MM = 360;
const uint8_t SCORE_ON = 2;
const uint8_t SCORE_MAX = 4;
const uint32_t DETECTION_COOLDOWN_MS = 120;
const uint32_t SERIAL_STATUS_INTERVAL_MS = 50;  // trame compacte 20 Hz

// Filtre entier: plus rapide qu'une médiane + float.
// 3/4 de la nouvelle mesure + 1/4 de l'ancienne valeur.
const int EMA_NUM = 3;
const int EMA_DEN = 4;
const int BASELINE_TRACK_DIV = 64;  // suivi lent du vide sans calcul flottant

int distanceVide[NB_CAPTEURS] = {2000, 2000, 2000, 2000};
int distanceFiltree[NB_CAPTEURS] = {2000, 2000, 2000, 2000};
int derniereBrute[NB_CAPTEURS] = {-1, -1, -1, -1};
int derniereChute[NB_CAPTEURS] = {0, 0, 0, 0};
uint8_t scoreDetection[NB_CAPTEURS] = {0, 0, 0, 0};
uint8_t invalidConsec[NB_CAPTEURS] = {0, 0, 0, 0};
bool capteurOK[NB_CAPTEURS] = {false, false, false, false};
bool detectionActive[NB_CAPTEURS] = {false, false, false, false};

bool ballonEtat = false;
uint32_t dernierEventMs = 0;
uint32_t dernierStatusSerialMs = 0;

void tcaSelect(uint8_t channel) {
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(1 << channel);
  Wire.endTransmission();
  delayMicroseconds(300);
}

int lireDistance(int i) {
  tcaSelect(i);
  int d = sensor[i].read();

  if (sensor[i].timeoutOccurred() || d <= 0 || d > 3000) {
    derniereBrute[i] = -1;
    if (invalidConsec[i] < 255) invalidConsec[i]++;
    return -1;
  }

  invalidConsec[i] = 0;
  derniereBrute[i] = d;
  distanceFiltree[i] = ((EMA_DEN - EMA_NUM) * distanceFiltree[i] + EMA_NUM * d) / EMA_DEN;
  return distanceFiltree[i];
}

void envoyerStatusSerial() {
  Serial.print("D,");
  Serial.print(millis());

  for (int i = 0; i < NB_CAPTEURS; i++) {
    Serial.print(',');
    Serial.print(i);
    Serial.print(',');
    Serial.print(capteurOK[i] ? 1 : 0);
    Serial.print(',');
    Serial.print(derniereBrute[i]);
    Serial.print(',');
    Serial.print(distanceFiltree[i]);
    Serial.print(',');
    Serial.print(distanceVide[i]);
    Serial.print(',');
    Serial.print(derniereChute[i]);
    Serial.print(',');
    Serial.print(detectionActive[i] ? 1 : 0);
    Serial.print(',');
    Serial.print(invalidConsec[i]);
  }

  Serial.print(',');
  Serial.println(ballonEtat ? 1 : 0);
}

void envoyerDetectionSerial(int capteur, int distance) {
  Serial.print("B,");
  Serial.print(millis());
  Serial.print(',');
  Serial.print(capteur);
  Serial.print(',');
  Serial.println(distance);
}

void calibrerVideRapide() {
  for (int i = 0; i < NB_CAPTEURS; i++) {
    if (!capteurOK[i]) continue;

    long somme = 0;
    int nb = 0;

    for (int j = 0; j < 8; j++) {
      int d = lireDistance(i);
      if (d > 700 && d < 3000) {
        somme += d;
        nb++;
      }
      delay(15);
    }

    int base = (nb > 0) ? (int)(somme / nb) : 2000;
    distanceVide[i] = base;
    distanceFiltree[i] = base;
    derniereBrute[i] = base;
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Wire.begin();
  Wire.setClock(400000);  // I2C rapide

  for (int i = 0; i < NB_CAPTEURS; i++) {
    tcaSelect(i);
    sensor[i].setTimeout(45);

    if (sensor[i].init()) {
      sensor[i].setDistanceMode(VL53L1X::Short);
      sensor[i].setMeasurementTimingBudget(20000);
      sensor[i].startContinuous(20);
      capteurOK[i] = true;
    }
  }

  calibrerVideRapide();
  Serial.println("V7 ready");
  Serial.println("D,ms,capteur,ok,raw,filtre,vide,chute,det,invalid,...,ballon");
  Serial.println("B,ms,capteur,distance_mm");
}

void loop() {
  bool ballonDetecte = false;
  int capteurDetecte = -1;
  int distanceDetectee = -1;
  uint32_t now = millis();

  for (int i = 0; i < NB_CAPTEURS; i++) {
    if (!capteurOK[i]) continue;

    int d = lireDistance(i);
    if (d < 0) {
      detectionActive[i] = false;
      if (scoreDetection[i] > 0) scoreDetection[i]--;
      continue;
    }

    int chute = distanceVide[i] - d;
    derniereChute[i] = chute;

    bool dansFenetre = (d >= DIST_MIN_MM && d <= DIST_MAX_MM && chute >= SEUIL_CHUTE_MM);

    if (dansFenetre) {
      if (scoreDetection[i] < SCORE_MAX) scoreDetection[i] += 2;
    } else {
      if (scoreDetection[i] > 0) scoreDetection[i]--;

      // Suivi lent de la référence vide, sans float et sans blocage.
      distanceVide[i] += (d - distanceVide[i]) / BASELINE_TRACK_DIV;
    }

    detectionActive[i] = (scoreDetection[i] >= SCORE_ON);

    if (detectionActive[i]) {
      ballonDetecte = true;
      capteurDetecte = i;
      distanceDetectee = d;
    }
  }

  if (ballonDetecte && !ballonEtat && (now - dernierEventMs) >= DETECTION_COOLDOWN_MS) {
    envoyerDetectionSerial(capteurDetecte, distanceDetectee);
    ballonEtat = true;
    dernierEventMs = now;
  }

  if (!ballonDetecte && ballonEtat) {
    ballonEtat = false;
  }

  if ((now - dernierStatusSerialMs) >= SERIAL_STATUS_INTERVAL_MS) {
    dernierStatusSerialMs = now;
    envoyerStatusSerial();
  }
}

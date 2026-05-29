#include <Wire.h>

#if !defined(ARDUINO_ARCH_ESP32)
#error "4capteurs_V8.ino est prevu pour une carte ESP32. Dans Arduino IDE, selectionner une carte ESP32 (ex: ESP32 Dev Module)."
#endif

#include <VL53L1X.h>

#define TCA_ADDR 0x70
#define NB_CAPTEURS 4

VL53L1X sensor[NB_CAPTEURS];

// --- V8: détection rapide/stable, Serial uniquement sur événement ballon ---
const int DIST_MIN_MM = 100;
const int DIST_MAX_MM = 1600;
const int SEUIL_CHUTE_MM = 320;
const uint8_t SCORE_ON = 3;
const uint8_t SCORE_MAX = 6;
const uint32_t DETECTION_COOLDOWN_MS = 140;

// EMA entière très légère: réactive et sans float.
// filtre = 3/4 nouvelle mesure + 1/4 ancienne mesure.
const int EMA_NUM = 3;
const int EMA_DEN = 4;
const int BASELINE_TRACK_DIV = 96;  // suivi lent du vide, non bloquant

int distanceVide[NB_CAPTEURS] = {2000, 2000, 2000, 2000};
int distanceFiltree[NB_CAPTEURS] = {2000, 2000, 2000, 2000};
uint8_t scoreDetection[NB_CAPTEURS] = {0, 0, 0, 0};
uint8_t invalidConsec[NB_CAPTEURS] = {0, 0, 0, 0};
bool capteurOK[NB_CAPTEURS] = {false, false, false, false};
bool capteurEnDetection[NB_CAPTEURS] = {false, false, false, false};

bool ballonEtat = false;
uint32_t dernierEventMs = 0;

void tcaSelect(uint8_t channel) {
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(1 << channel);
  Wire.endTransmission();
  delayMicroseconds(200);
}

int lireDistanceRapide(int i) {
  tcaSelect(i);
  int d = sensor[i].read();

  if (sensor[i].timeoutOccurred() || d <= 0 || d > 3000) {
    if (invalidConsec[i] < 255) invalidConsec[i]++;
    return -1;
  }

  invalidConsec[i] = 0;
  distanceFiltree[i] = ((EMA_DEN - EMA_NUM) * distanceFiltree[i] + EMA_NUM * d) / EMA_DEN;
  return distanceFiltree[i];
}

void envoyerDetectionSerial(int capteur, int distance) {
  Serial.print("BALLON,C");
  Serial.print(capteur);
  Serial.print(",");
  Serial.print(distance);
  Serial.print(",");
  Serial.println(millis());
}

void calibrerVideRapide() {
  for (int i = 0; i < NB_CAPTEURS; i++) {
    if (!capteurOK[i]) continue;

    long somme = 0;
    int nb = 0;

    for (int j = 0; j < 8; j++) {
      int d = lireDistanceRapide(i);
      if (d > 700 && d < 3000) {
        somme += d;
        nb++;
      }
      delay(12);
    }

    int base = (nb > 0) ? (int)(somme / nb) : 2000;
    distanceVide[i] = base;
    distanceFiltree[i] = base;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin();
  Wire.setClock(400000);

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
}

void loop() {
  bool ballonDetecte = false;
  int capteurDetecte = -1;
  int distanceDetectee = -1;
  uint32_t now = millis();

  for (int i = 0; i < NB_CAPTEURS; i++) {
    if (!capteurOK[i]) continue;

    int d = lireDistanceRapide(i);
    if (d < 0) {
      if (scoreDetection[i] > 0) scoreDetection[i]--;
      capteurEnDetection[i] = false;
      continue;
    }

    int chute = distanceVide[i] - d;
    bool detectionPossible = (d >= DIST_MIN_MM && d <= DIST_MAX_MM && chute >= SEUIL_CHUTE_MM);

    if (detectionPossible) {
      if (scoreDetection[i] <= SCORE_MAX - 3) {
        scoreDetection[i] += 3;
      } else {
        scoreDetection[i] = SCORE_MAX;
      }
    } else {
      if (scoreDetection[i] > 0) scoreDetection[i]--;

      // Baseline adaptatif uniquement hors détection: stable, rapide, sans pause.
      distanceVide[i] += (d - distanceVide[i]) / BASELINE_TRACK_DIV;
    }

    capteurEnDetection[i] = (scoreDetection[i] >= SCORE_ON);

    if (capteurEnDetection[i]) {
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
}

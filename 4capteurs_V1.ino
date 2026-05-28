#include <Wire.h>
#include <VL53L1X.h>

#define TCA_ADDR 0x70
#define NB_CAPTEURS 4

VL53L1X sensor[NB_CAPTEURS];

// Réglages
const int DIST_MIN_MM = 120;
const int DIST_MAX_MM = 1200;
const int SEUIL_CHUTE_MM = 500;
const int CONFIRMATION = 2;

int distanceVide[NB_CAPTEURS] = {0, 0, 0, 0};
int compteur[NB_CAPTEURS] = {0, 0, 0, 0};
bool capteurOK[NB_CAPTEURS] = {false, false, false, false};

bool ballonEtat = false;

void tcaSelect(uint8_t channel) {
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(1 << channel);
  Wire.endTransmission();
}

int lireDistance(int i) {
  tcaSelect(i);
  delay(2);

  int d = sensor[i].read();

  if (sensor[i].timeoutOccurred()) return -1;
  if (d <= 0) return -1;
  if (d > 4000) return -1;

  return d;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin();
  Wire.setClock(100000);

  for (int i = 0; i < NB_CAPTEURS; i++) {
    tcaSelect(i);
    //delay(100);

    sensor[i].setTimeout(500);

    if (sensor[i].init()) {
      sensor[i].setDistanceMode(VL53L1X::Long);
      sensor[i].setMeasurementTimingBudget(50000);
      sensor[i].startContinuous(50);
      capteurOK[i] = true;
    }
  }

  //delay(10);

  // Calibration à vide
  for (int i = 0; i < NB_CAPTEURS; i++) {
    if (!capteurOK[i]) continue;

    long somme = 0;
    int nb = 0;

    for (int j = 0; j < 20; j++) {
      int d = lireDistance(i);

      if (d > 1000 && d < 3500) {
        somme += d;
        nb++;
      }

      delay(60);
    }

    if (nb > 0) {
      distanceVide[i] = somme / nb;
    } else {
      distanceVide[i] = 2000;
    }
  }
}

void loop() {
  bool ballonDetecte = false;
  int capteurDetecte = -1;
  int distanceDetectee = -1;

  for (int i = 0; i < NB_CAPTEURS; i++) {
    if (!capteurOK[i]) continue;

    int d = lireDistance(i);

    if (d == -1) {
      compteur[i] = 0;
      continue;
    }

    int chute = distanceVide[i] - d;

    bool detectionCapteur =
      d >= DIST_MIN_MM &&
      d <= DIST_MAX_MM &&
      chute >= SEUIL_CHUTE_MM;

    if (detectionCapteur) {
      compteur[i]++;
    } else {
      compteur[i] = 0;
    }

    if (compteur[i] >= CONFIRMATION) {
      ballonDetecte = true;
      capteurDetecte = i;
      distanceDetectee = d;
    }
  }

  // Affiche uniquement au moment où le ballon apparaît
  if (ballonDetecte && !ballonEtat) {
    Serial.print("BALLON | Capteur C");
    Serial.print(capteurDetecte);
    Serial.print(" | Distance ");
    Serial.print(distanceDetectee);
    Serial.println(" mm");

    ballonEtat = true;
  }

  // Reset pour permettre une nouvelle détection
  if (!ballonDetecte && ballonEtat) {
    ballonEtat = false;
  }
}
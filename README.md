# CapteurRugby

## Versions
- `4capteurs_V1.ino` : version initiale.
- `4capteurs_V2.ino` : version améliorée pour lecture plus stable + interface Web de supervision/réglage.

## V2 - points principaux
- Filtrage des distances par **médiane glissante** puis **EMA** (plus stable, mais réactif pour un ballon rapide).
- Détection par capteur avec validation multi-lectures (`CONFIRMATION`) + anti double-trigger (`DETECTION_COOLDOWN_MS`).
- Seuils **Min/Max par capteur** modifiables via interface Web.
- Sauvegarde persistante des seuils dans la mémoire non volatile (Preferences).
- API locale:
  - `GET /api/status` : état en temps réel (raw, filtré, chute, détection, seuils).
  - `POST /api/config` : mise à jour/sauvegarde des seuils.

## Interface Web
1. Adapter `WIFI_SSID` et `WIFI_PASS` dans `4capteurs_V2.ino`.
2. Flasher la carte (ESP32 conseillé).
3. Lire l'IP dans le moniteur série (`115200`) puis ouvrir cette IP dans un navigateur.

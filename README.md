# CapteurRugby

## Versions
- `4capteurs_V1.ino` : version initiale.
- `4capteurs_V2.ino` : version améliorée pour lecture plus stable + interface Web de supervision/réglage.
- `4capteurs_V3.ino` : version optimisée terrain (AP+STA confirmé, recalibration à la demande, détection plus réactive).

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


## Connexion WiFi (important)
- **Les deux sont possibles** dans la V2:
  - WiFi local (routeur) via `WIFI_SSID` / `WIFI_PASS`.
  - Connexion directe à l'ESP32 via son point d'accès `AP_SSID` / `AP_PASS`.
- Le code démarre en `WIFI_AP_STA`, donc même sans routeur disponible, l'interface web reste accessible en se connectant directement à l'ESP32 (IP AP généralement `192.168.4.1`).


## V3 - nouveautés
- Conserve le mode réseau mixte `WIFI_AP_STA` (WiFi local + connexion directe ESP32).
- Ajoute un bouton **Recalibrer vide** dans l'interface web (`POST /api/recalibrate`).
- Ajuste les paramètres de filtrage/détection pour passage rapide du ballon:
  - `MEDIAN_WINDOW=3`, `EMA_ALPHA=0.55`, `SEUIL_CHUTE_MM=380`, `COOLDOWN=180ms`, `LIBERATION=2`.

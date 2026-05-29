# CapteurRugby

## Versions
- `4capteurs_V1.ino` : version initiale.
- `4capteurs_V2.ino` : version améliorée pour lecture plus stable + interface Web de supervision/réglage.
- `4capteurs_V3.ino` : version optimisée terrain (AP+STA confirmé, recalibration à la demande, détection plus réactive).
- `4capteurs_V5.ino` : version stabilité pure, sans interface Web.
- `4capteurs_V6.ino` : version stabilité + télémétrie série PC.
- `4capteurs_V7.ino` : version plus légère et plus rapide, avec télémétrie série compacte.
- `4capteurs_V8.ino` : version ultra légère/rapide, sans télémétrie continue, avec Serial uniquement lors d’une détection ballon.

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


## Dépannage connexion AP (ESP32 direct)
- Si le téléphone n'arrive pas à se connecter au SSID `CapteurRugby-ESP32-V3`, vérifier dans le moniteur série que la ligne `AP actif` apparaît.
- V3 force le canal 6, coupe le mode sleep WiFi et active une puissance radio élevée pour améliorer la stabilité.
- Si le mode sécurisé WPA2 échoue, la V3 tente automatiquement un AP ouvert de secours (même SSID).
- Après connexion AP directe, ouvrir `http://192.168.4.1`.


## Stabilité V3 (anti-plantage)
- Réduction des allocations dynamiques dans la boucle web: JSON de statut généré via buffer fixe (`snprintf`) au lieu de concaténations `String`.
- Page HTML servie depuis la flash (`PROGMEM` + `send_P`) pour limiter la fragmentation mémoire RAM.
- Rafraîchissement UI ralenti à 400 ms pour réduire la pression réseau/CPU.


## Version V5 (stabilité renforcée, base V1)
- Nouvelle base `4capteurs_V5.ino` reconstruite depuis la logique V1 avec priorité à la stabilité runtime.
- Lecture stable: filtre **médiane glissante (5)** + **EMA** pour réduire bruit et pics sans perdre trop de réactivité.
- Anti-faux déclenchements: compteurs ON/OFF, cooldown inter-événements, et gestion des séries de mesures invalides.
- Robustesse terrain: suivi lent de la distance de référence (drift de fond) et recalibration automatique à vide après période d'inactivité.
- Aucun serveur Web dans cette version: firmware orienté détection pure et stabilité capteurs.


## Version V6 (V5 + télémétrie série PC)
- Nouvelle version `4capteurs_V6.ino` basée sur la V5, sans interface Web, avec priorité à la stabilité et à l'observation depuis un PC.
- Envoi automatique d'une trame série `DATA` toutes les 100 ms avec, pour chaque capteur: état OK, distance brute, distance filtrée, référence vide, chute, détection active et nombre de lectures invalides.
- Envoi immédiat d'une trame `BALLON` dès qu'un ballon est détecté, avec horodatage `millis()`, capteur concerné et distance en millimètres.
- Format prévu pour être lu dans le moniteur série Arduino ou par un script PC: `DATA,ms=...,C0_raw=...,C0_stable=...,C0_vide=...,C0_chute=...,C0_det=...,ballon=...`.
- La recalibration automatique bloquante de la V5 n'est pas reprise dans la V6: la référence vide est maintenue par suivi lent pour éviter de bloquer la boucle de détection pendant plusieurs secondes.


## Version V7 (plus légère et plus rapide)
- Nouvelle version `4capteurs_V7.ino` dérivée de la V6 mais optimisée pour la rapidité: pas de serveur Web, pas de `String`, pas de tri médiane, pas de calcul flottant dans la boucle.
- Lecture capteurs accélérée avec I2C à 400 kHz, mode VL53L1X `Short`, budget de mesure 20 ms, période continue 20 ms et timeout raccourci.
- Filtrage allégé par EMA entière (`3/4` nouvelle mesure, `1/4` ancienne mesure) et score de détection léger pour garder une protection anti-faux positifs.
- Télémétrie série compacte: trame `D` toutes les 50 ms et événement `B` immédiat lors d'une détection ballon.
- Format compact: `D,ms,capteur,ok,raw,filtre,vide,chute,det,invalid,...,ballon` et `B,ms,capteur,distance_mm`.


## Erreur compilation Windows / Arduino IDE (`TwoWire` ou `USI_TWI`)
Si la compilation affiche une erreur du type `TwoWire has not been declared` ou `class USI_TWI has no member named setClock`, la mauvaise carte est généralement sélectionnée dans Arduino IDE.
- Sélectionner une carte **ESP32** dans `Outils > Type de carte` (par exemple `ESP32 Dev Module`).
- Ne pas compiler ce code pour une carte ATtiny / USI: la bibliothèque Pololu `VL53L1X` utilisée ici attend le bus I2C Arduino `TwoWire`, alors que `USI_TWI` ne fournit pas les mêmes fonctions.
- Vérifier que le sketch garde bien `#include <Wire.h>` avant `#include <VL53L1X.h>`.
- Installer le package de cartes ESP32 Espressif et la bibliothèque Pololu `VL53L1X` depuis le gestionnaire de bibliothèques Arduino.


## Version V8 (ultra légère, rapide, Serial seulement sur détection)
- Nouvelle version `4capteurs_V8.ino` optimisée pour la détection rapide d’un ballon lancé à la main: aucun serveur Web, aucune trame de télémétrie continue, aucun `String`, aucun calcul flottant et aucun tri médiane.
- Le seul message série en fonctionnement normal est envoyé lors d'une détection: `BALLON,Cx,distance_mm,millis`.
- Lecture rapide: I2C 400 kHz, mode VL53L1X `Short`, budget de mesure 20 ms, période continue 20 ms et timeout 45 ms.
- Stabilité conservée avec EMA entière réactive, score de détection/hystérésis, cooldown anti double-détection et suivi lent du vide hors détection.
- À utiliser en priorité si l’objectif est la vitesse et la stabilité terrain plutôt que l’observation détaillée depuis le PC.

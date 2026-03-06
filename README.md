# CRSD - Système Domotique Complet

## Vue d'ensemble

Le système CRSD est une solution domotique complète avec auto-découverte et auto-configuration des équipements. Il comprend :

- **ESP32 Controller** : Contrôleur principal qui découvre et configure automatiquement tous les équipements
- **ESP32-CAM** : Caméra de surveillance avec streaming vidéo
- **ESP32 Gâche** : Contrôleur de gâche électrique via MQTT
- **Serveur Python** : Interface web, gestion utilisateurs, broker MQTT et streaming vidéo

## Architecture

```
[ESP32 Controller] ←→ [WiFi Maison] ←→ [Serveur Python]
                                    ↑
                              [ESP32-CAM] (streaming vidéo)
                                    ↑
                              [ESP32 Gâche] (MQTT)
```

## Installation Automatique

### 1. Préparation
- Téléverser le code sur chaque ESP32
- Brancher tous les équipements
- Démarrer le serveur Python

### 2. Configuration via Controller
1. Se connecter au WiFi `CRSD-Setup-xxxxxxxx`
2. Ouvrir http://192.168.4.1
3. Configurer :
   - Réseau WiFi maison
   - Adresse serveur
   - Email/mot de passe utilisateur
4. Cliquer "Installer"

### 3. Processus Automatique
Le Controller va automatiquement :
- Se connecter au WiFi maison
- Découvrir la caméra ESP32-CAM
- Configurer la caméra avec les paramètres réseau
- Découvrir la gâche ESP32
- Configurer la gâche avec MQTT
- Enregistrer l'utilisateur sur le serveur

## Interfaces Web

### Page d'accueil
- **URL** : http://[serveur]:5000/
- **Login** : email/password configuré
- Affiche les informations utilisateur et liens vers les modules

### Vidéosurveillance
- **URL** : http://[serveur]:5000/cam.html
- Streaming vidéo en temps réel
- Contrôles caméra (résolution, qualité, effets)
- Navigation vers gestion gâches

### Gestion Gâches
- **URL** : http://[serveur]:5000/gache.html
- Dashboard avec statistiques
- Contrôle individuel des gâches
- Communication MQTT temps réel via WebSocket

## API

### Utilisateurs
- `GET /users` : Liste des utilisateurs
- `POST /register` : Enregistrer un utilisateur

### Gâches
- `GET /gaches` : Liste des gâches configurées

### Caméra
- `GET /stream` : Flux vidéo MJPEG
- `GET /control?var=X&val=Y` : Contrôle caméra

## Communication

### ESP32-CAM ↔ Serveur
- **Protocole** : TCP (port 8000 pour vidéo, 5000 pour commandes)
- **Authentification** : Token `CAM_SECRET_2024_XyZ9`

### ESP32 Gâche ↔ Serveur
- **Protocole** : MQTT (port 1883)
- **Topics** :
  - `crsd/{device_id}/command` : Commandes (OPEN, CLOSE, TOGGLE)
  - `crsd/{device_id}/state` : État de la gâche

### Interface Web ↔ Serveur
- **WebSocket** : ws://[serveur]:9001 pour MQTT temps réel
- **HTTP** : Port 5000 pour l'interface web

## Sécurité

### Authentification Multi-niveaux
1. **Interface Web** : HTTP Basic Auth avec email/password
2. **ESP32-CAM** : Token d'authentification TCP
3. **ESP32 Gâche** : Token d'authentification TCP + MQTT

### Tokens de Sécurité
- `CAM_SECRET_2024_XyZ9` : Authentification caméra
- `GACHE_SECRET_2024_XyZ9` : Authentification gâche

### Auto-fermeture Sécurisée
- Les gâches se ferment automatiquement après 3 secondes
- Bouton manuel d'urgence sur chaque gâche

## Dépendances

### ESP32
- WiFi
- WebServer
- PubSubClient (MQTT)
- ArduinoJson
- Preferences

### Serveur Python
```bash
pip install flask flask-cors paho-mqtt websockets
```

## Configuration Réseau

### Ports Utilisés
- **80** : Interface web Controller (mode setup)
- **5000** : Interface web serveur + API
- **5001** : Commandes TCP gâche
- **5002** : Discovery UDP gâche
- **8000** : Streaming vidéo caméra
- **9001** : WebSocket MQTT
- **1883** : Broker MQTT

### Adresses IP par Défaut
- **Controller Setup** : 192.168.4.1
- **Caméra Setup** : 192.168.50.1
- **Gâche Setup** : 192.168.60.1

## Utilisation

### Contrôle Gâches
1. Ouvrir http://[serveur]:5000/gache.html
2. Voir le dashboard avec toutes les gâches
3. Cliquer sur une gâche pour contrôles détaillés
4. Utiliser les boutons Ouvrir/Fermer/Basculer

### Surveillance Vidéo
1. Ouvrir http://[serveur]:5000/cam.html
2. Voir le flux vidéo en temps réel
3. Ajuster les paramètres via l'icône ⋮
4. Capturer des photos si nécessaire

## Dépannage

### Caméra Non Trouvée
- Vérifier que l'ESP32-CAM est alimenté
- Vérifier le réseau WiFi `CRSD-CAM-xxxxxxxx`
- Redémarrer le Controller

### Gâche Non Détectée
- Vérifier l'alimentation de l'ESP32 gâche
- Vérifier le réseau WiFi `CRSD-GACHE-xxxxxxxx`
- Vérifier le broker MQTT sur le serveur

### Reset Configuration
- Maintenir le bouton BOOT au démarrage du Controller
- Ou re-téléverser le code (efface automatiquement)

## Évolutions Futures

- Support TLS/SSL
- Rotation automatique des tokens
- Hachage des mots de passe
- Interface mobile native
- Support multi-caméras
- Capteurs supplémentaires (température, mouvement)
- Éclairage intelligent
- Notifications push
# Guide d'Installation CRSD

## 🚀 Installation Rapide

### 1. Préparation du Matériel
- **ESP32 Controller** : ESP32 standard
- **ESP32-CAM** : Module caméra ESP32-CAM + adaptateur FTDI
- **ESP32 Gâche** : ESP32 + relais + gâche électrique
- **Serveur** : PC/Raspberry Pi avec Python 3.7+

### 2. Téléversement du Code

#### ESP32 Controller
```bash
# Ouvrir CRSD/ESP32_Controller/ESP32_Controller.ino dans Arduino IDE
# Sélectionner carte: ESP32 Dev Module
# Téléverser
```

#### ESP32-CAM
```bash
# Ouvrir CRSD/CameraWebServer/CameraWebServer.ino dans Arduino IDE
# Sélectionner carte: AI Thinker ESP32-CAM
# Connecter GPIO0 à GND pour mode programmation
# Téléverser
# Déconnecter GPIO0 après téléversement
```

#### ESP32 Gâche
```bash
# Ouvrir CRSD/ESP32_Gache/ESP32_Gache.ino dans Arduino IDE
# Sélectionner carte: ESP32 Dev Module
# Connecter relais sur GPIO 15
# Téléverser
```

### 3. Démarrage du Serveur

#### Windows
```bash
cd CRSD/temp_Cam
start_server.bat
```

#### Linux - Installation Automatique
```bash
cd CRSD/temp_Cam
chmod +x install_linux.sh
./install_linux.sh
```

#### Linux - Installation Manuelle
```bash
cd CRSD/temp_Cam
python3 start_server.py
```

#### macOS
```bash
cd CRSD/temp_Cam
python3 start_server.py
```

### 4. Configuration Automatique

1. **Connecter au Controller**
   - WiFi : `CRSD-Setup-xxxxxxxx`
   - URL : http://192.168.4.1

2. **Remplir le formulaire**
   - Réseau WiFi maison
   - Adresse IP du serveur
   - Email/mot de passe utilisateur

3. **Cliquer "Installer"**
   - Le Controller configure automatiquement tous les équipements

### 5. Accès aux Interfaces

- **Dashboard** : http://[serveur]:5000/
- **Caméra** : http://[serveur]:5000/cam.html
- **Gâches** : http://[serveur]:5000/gache.html

## 🔧 Configuration Avancée

### Installation Serveur Linux (Automatique)

Le script `install_linux.sh` configure automatiquement :
- **Python 3.7+** et environnement virtuel
- **Mosquitto MQTT Broker** 
- **Dépendances Python** (Flask, MQTT, WebSocket)
- **Service systemd** pour démarrage automatique
- **Règles firewall** pour les ports nécessaires
- **Scripts de gestion** (start, stop, status, restart)

```bash
cd CRSD/temp_Cam
chmod +x install_linux.sh
./install_linux.sh
```

### Gestion du Service Linux

Après installation automatique :
```bash
# Démarrer le serveur
sudo systemctl start crsd-server

# Arrêter le serveur  
sudo systemctl stop crsd-server

# Voir le statut
sudo systemctl status crsd-server

# Voir les logs
sudo journalctl -u crsd-server -f

# Scripts de gestion
./start_server.sh    # Démarrage manuel
./status.sh          # Statut complet
./stop_server.sh     # Arrêt
./restart_server.sh  # Redémarrage
```

### Désinstallation Linux

```bash
chmod +x uninstall_linux.sh
./uninstall_linux.sh
```

### Broker MQTT (pour gâches)

#### Windows
1. Télécharger Mosquitto : https://mosquitto.org/download/
2. Installer et démarrer le service

#### Linux
```bash
sudo apt update
sudo apt install mosquitto mosquitto-clients
sudo systemctl start mosquitto
sudo systemctl enable mosquitto
```

#### macOS
```bash
brew install mosquitto
brew services start mosquitto
```

### Adresses IP

Modifier dans `stream_server.py` si nécessaire :
```python
CAMERA_IP = "192.168.100.66"  # IP de votre ESP32-CAM
MQTT_BROKER_HOST = "localhost"  # IP du broker MQTT
```

## 🛠️ Dépannage

### Problème : Caméra non trouvée
**Solution :**
- Vérifier alimentation ESP32-CAM
- Redémarrer ESP32-CAM
- Vérifier réseau WiFi `CRSD-CAM-xxxxxxxx`

### Problème : Gâche non détectée
**Solution :**
- Vérifier broker MQTT actif
- Vérifier alimentation ESP32 gâche
- Vérifier réseau WiFi `CRSD-GACHE-xxxxxxxx`

### Problème : Serveur ne démarre pas
**Solution :**
```bash
pip install flask flask-cors paho-mqtt websockets
```

### Reset Configuration
- Maintenir bouton BOOT au démarrage du Controller
- Ou re-téléverser le code

## 📱 Utilisation

### Contrôle Gâches
1. Aller sur http://[serveur]:5000/gache.html
2. Voir toutes les gâches dans le dashboard
3. Cliquer sur une gâche pour contrôles détaillés
4. Utiliser boutons Ouvrir/Fermer/Basculer

### Surveillance Vidéo
1. Aller sur http://[serveur]:5000/cam.html
2. Voir flux vidéo temps réel
3. Ajuster paramètres via icône ⋮
4. Capturer photos si nécessaire

## 🔒 Sécurité

### Tokens par Défaut
- Caméra : `CAM_SECRET_2024_XyZ9`
- Gâche : `GACHE_SECRET_2024_XyZ9`

### Recommandations
- Changer les tokens en production
- Utiliser HTTPS en production
- Configurer firewall approprié
- Utiliser mots de passe forts

## 📞 Support

En cas de problème :
1. Vérifier les logs série des ESP32
2. Vérifier les logs du serveur Python
3. Tester la connectivité réseau
4. Redémarrer tous les équipements

## 🎯 Fonctionnalités

### ✅ Implémenté
- Auto-découverte équipements
- Auto-configuration réseau
- Streaming vidéo temps réel
- Contrôle gâches MQTT
- Interface web responsive
- Authentification utilisateur

### 🔄 En Développement
- Support multi-caméras
- Notifications push
- Interface mobile native
- Capteurs supplémentaires
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

// ==================== CONSTANTES ====================
const char* VERSION = "1.0.0";
const char* AP_SSID = "CRSD-Setup-";
const char* AP_PASSWORD = "";
const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_GATEWAY(192, 168, 4, 1);
const IPAddress AP_SUBNET(255, 255, 255, 0);

const char* AUTH_TOKEN = "CAM_SECRET_2024_XyZ9";

WebServer server(80);
DNSServer dnsServer;
Preferences prefs;

enum State { SETUP_MODE, CONNECTING_WIFI, DISCOVERING_CAM, CONFIGURING_CAM, RUNNING };
State currentState = SETUP_MODE;

struct Config {
  char wifiSsid[64] = "";
  char wifiPass[64] = "";
  char serverIp[64] = "";
  int serverPort = 8000;
  char userEmail[64] = "";
  char userPass[64] = "";
} userConfig;

// ==================== MQTT CLIENT ====================
WiFiClient mqttWifiClient;
PubSubClient mqttClient(mqttWifiClient);
const int MQTT_PORT = 1883;

// ==================== TCP CLIENTS ====================
const int GACHE_TCP_PORT = 5001;
const int CONTROLLER_TCP_PORT = 5003;  // Port pour que la gâche se connecte à nous

// ==================== TCP SERVER POUR GÂCHE ====================
WiFiServer gacheTcpServer(CONTROLLER_TCP_PORT);
WiFiClient persistentGacheClient;  // Connexion persistante avec la gâche
bool gacheClientConnected = false;

// ==================== ÉTAT GÂCHE ====================
String gacheDeviceId = "";
String gacheState = "closed";
unsigned long lastGacheStateUpdate = 0;
String homeWifiIp = "";  // IP du Controller sur le WiFi maison

#define LED_PIN 2
#define RESET_BUTTON_PIN 0

const char* indexHtml = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>CRSD - Configuration</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; }
    body { background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; display: flex; align-items: center; justify-content: center; padding: 20px; }
    .container { background: white; border-radius: 20px; box-shadow: 0 20px 60px rgba(0,0,0,0.3); width: 100%; max-width: 400px; overflow: hidden; }
    .header { background: #667eea; color: white; padding: 30px; text-align: center; }
    .header h1 { font-size: 24px; margin-bottom: 5px; }
    .header p { opacity: 0.9; font-size: 14px; }
    .content { padding: 30px; }
    .step { display: none; }
    .step.active { display: block; }
    .form-group { margin-bottom: 20px; }
    label { display: block; margin-bottom: 8px; font-weight: 600; color: #333; font-size: 14px; }
    input, select { width: 100%; padding: 12px 15px; border: 2px solid #e1e1e1; border-radius: 10px; font-size: 16px; transition: border-color 0.3s; }
    input:focus, select:focus { outline: none; border-color: #667eea; }
    button { width: 100%; padding: 15px; background: #667eea; color: white; border: none; border-radius: 10px; font-size: 16px; font-weight: 600; cursor: pointer; transition: transform 0.2s, background 0.3s; }
    button:hover { background: #5a6fd6; transform: scale(1.02); }
    button:disabled { background: #ccc; cursor: not-allowed; transform: none; }
    .status { text-align: center; padding: 20px; }
    .spinner { width: 50px; height: 50px; border: 4px solid #f3f3f3; border-top: 4px solid #667eea; border-radius: 50%; animation: spin 1s linear infinite; margin: 0 auto 20px; }
    @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
    .success { color: #28a745; font-size: 48px; margin-bottom: 20px; }
    .error { color: #dc3545; background: #f8d7da; padding: 15px; border-radius: 10px; margin-bottom: 20px; text-align: center; }
    .wifi-list { max-height: 200px; overflow-y: auto; border: 2px solid #e1e1e1; border-radius: 10px; }
    .wifi-item { padding: 12px 15px; border-bottom: 1px solid #eee; cursor: pointer; transition: background 0.2s; }
    .wifi-item:hover { background: #f5f5f5; }
    .wifi-item:last-child { border-bottom: none; }
    .wifi-item.selected { background: #667eea; color: white; }
    .progress-bar { width: 100%; height: 8px; background: #e1e1e1; border-radius: 4px; overflow: hidden; margin: 20px 0; }
    .progress-fill { height: 100%; background: linear-gradient(90deg, #667eea, #764ba2); transition: width 0.5s; }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1>🏠 Configuration CRSD</h1>
      <p>Assistant d'installation</p>
    </div>
    <div class="content">
      <div id="step1" class="step active">
        <div class="form-group">
          <label>📶 Réseau WiFi</label>
          <div id="wifiList" class="wifi-list">
            <div class="wifi-item" onclick="refreshWifi()">🔄 Actualiser...</div>
          </div>
          <input type="text" id="wifiSsid" placeholder="Ou tapez manuellement le nom" onchange="manualWifiSelected()">
        </div>
        <div class="form-group">
          <label>🔑 Mot de passe WiFi</label>
          <input type="password" id="wifiPass" placeholder="Laissez vide si ouvert">
        </div>
        <button onclick="nextStep(2)">Suivant →</button>
      </div>

      <div id="step2" class="step">
        <div class="form-group">
          <label>🌐 Adresse serveur</label>
          <input type="text" id="serverIp" placeholder="cloud.crsd.com ou IP">
        </div>
        <div class="form-group">
          <label>🔌 Port serveur</label>
          <input type="number" id="serverPort" value="8000">
        </div>
        <button onclick="nextStep(3)">Suivant →</button>
        <button onclick="nextStep(1)" style="background: #6c757d; margin-top: 10px;">← Retour</button>
      </div>

      <div id="step3" class="step">
        <div class="form-group">
          <label>📧 Email</label>
          <input type="email" id="userEmail" placeholder="votre@email.com">
        </div>
        <div class="form-group">
          <label>🔐 Mot de passe</label>
          <input type="password" id="userPass" placeholder="Mot de passe">
        </div>
        <button onclick="startConfig()">🚀 Installer</button>
        <button onclick="nextStep(2)" style="background: #6c757d; margin-top: 10px;">← Retour</button>
      </div>

      <div id="step4" class="step">
        <div class="status">
          <div class="spinner"></div>
          <p id="statusText">Connexion au WiFi...</p>
          <div class="progress-bar">
            <div id="progressFill" class="progress-fill" style="width: 0%"></div>
          </div>
        </div>
      </div>

      <div id="step5" class="step">
        <div class="status">
          <div class="success">✅</div>
          <h2 style="margin-bottom: 20px;">Installation terminée !</h2>
          <p>Votre caméra est prête.</p>
          <p style="margin-top: 10px; color: #666;">Redémarrage en mode normal...</p>
        </div>
      </div>

      <div id="step6" class="step">
        <div class="status">
          <div class="error" id="errorMsg">Erreur</div>
          <button onclick="nextStep(1)">← Réessayer</button>
        </div>
      </div>
    </div>
  </div>

  <script>
    let wifiNetworks = [];

    function showStep(n) {
      document.querySelectorAll('.step').forEach(s => s.classList.remove('active'));
      document.getElementById('step' + n).classList.add('active');
    }

    function refreshWifi() {
      fetch('/wifi').then(r => r.json()).then(data => {
        wifiNetworks = data.networks;
        const list = document.getElementById('wifiList');
        list.innerHTML = data.networks.map((w, i) => 
          `<div class="wifi-item" onclick="selectWifi(${i})" id="wifi${i}">📶 ${w.ssid} ${w.encrypted ? '🔒' : '📡'}</div>`
        ).join('');
      });
    }

    function selectWifi(i) {
      document.querySelectorAll('.wifi-item').forEach(w => w.classList.remove('selected'));
      document.getElementById('wifi' + i).classList.add('selected');
      document.getElementById('wifiSsid').value = wifiNetworks[i].ssid;
      document.getElementById('wifiPass').value = '';
    }

    function manualWifiSelected() {
      document.querySelectorAll('.wifi-item').forEach(w => w.classList.remove('selected'));
    }

    function nextStep(n) {
      if (n === 2) {
        const ssid = document.getElementById('wifiSsid').value;
        if (!ssid) { alert('Sélectionnez un réseau WiFi'); return; }
      }
      showStep(n);
      if (n === 1) refreshWifi();
    }

    function startConfig() {
      const data = {
        ssid: document.getElementById('wifiSsid').value,
        pass: document.getElementById('wifiPass').value,
        serverIp: document.getElementById('serverIp').value,
        serverPort: parseInt(document.getElementById('serverPort').value),
        email: document.getElementById('userEmail').value,
        passUser: document.getElementById('userPass').value
      };
      
      showStep(4);
      document.getElementById('statusText').textContent = 'Connexion au WiFi...';
      document.getElementById('progressFill').style.width = '10%';

      fetch('/config', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(data)
      }).then(r => r.json()).then(data => {
        if (data.success) {
          runProgress();
        } else {
          document.getElementById('errorMsg').textContent = data.error;
          showStep(6);
        }
      });
    }

    function runProgress() {
      const steps = [
        { pct: 20, msg: 'Connexion au WiFi...' },
        { pct: 40, msg: 'Recherche caméra...' },
        { pct: 60, msg: 'Configuration caméra...' },
        { pct: 80, msg: 'Enregistrement...' },
        { pct: 100, msg: 'Terminé !' }
      ];
      
      let i = 0;
      const interval = setInterval(() => {
        if (i >= steps.length) {
          clearInterval(interval);
          showStep(5);
          setTimeout(() => window.location.reload(), 3000);
          return;
        }
        document.getElementById('progressFill').style.width = steps[i].pct + '%';
        document.getElementById('statusText').textContent = steps[i].msg;
        i++;
      }, 1500);
    }

    refreshWifi();
  </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== CRSD Controller v" + String(VERSION) + " ===");
  
  pinMode(LED_PIN, OUTPUT);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, LOW);

  prefs.begin("crsd", false);
  
  // Vérifier si c'est un redémarrage après configuration
  bool isConfigRestart = prefs.getBool("configRestart", false);
  
  if (!isConfigRestart) {
    // Premier démarrage après téléversement → Effacer config
    prefs.clear();
    Serial.println("🔄 Config effacée automatiquement (téléversement)");
  } else {
    // Redémarrage après configuration → Garder config
    prefs.putBool("configRestart", false);  // Reset flag
    Serial.println("🔄 Redémarrage après configuration - Config conservée");
  }

  // Vérifier si bouton RESET pressé au démarrage
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    Serial.println("🔄 BOUTON RESET DÉTECTÉ - Effacement config...");
    prefs.clear();
    Serial.println("Config effacée - redémarrage en mode SETUP");
    delay(1000);
    ESP.restart();
  }

  String savedSsid = prefs.getString("ssid", "");
  
  if (savedSsid.length() > 0) {
    Serial.println("Config trouvée - Mode NORMAL");
    currentState = RUNNING;
    startNormalMode();
  } else {
    Serial.println("Pas de config - Mode SETUP");
    currentState = SETUP_MODE;
    startSetupMode();
  }
}

void startSetupMode() {
  Serial.println("Mode SETUP activé");
  
  uint32_t chipId = ESP.getEfuseMac();
  String apSsid = String(AP_SSID) + String(chipId, HEX);
  
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
  WiFi.softAP(apSsid.c_str(), AP_PASSWORD);
  
  Serial.println("AP started: " + apSsid);
  Serial.println("IP: " + WiFi.softAPIP().toString());
  
  dnsServer.start(53, "*", WiFi.softAPIP());
  
  server.on("/", []() { server.send(200, "text/html", indexHtml); });
  server.on("/wifi", handleWifiList);
  server.on("/config", HTTP_POST, handleConfig);
  server.onNotFound([]() { server.sendHeader("Location", "http://192.168.4.1/", true); server.send(302, "text/plain", ""); });
  
  server.begin();
  Serial.println("Web server started");
  
  blinkLed(100, 5);
}

void blinkLed(int interval, int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(interval);
    digitalWrite(LED_PIN, LOW);
    delay(interval);
  }
}

// ==================== MQTT CALLBACKS ====================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.println("[MQTT] " + String(topic) + " = " + message);
  
  // Si commande pour la gâche
  if (String(topic).indexOf("/command") >= 0 && gacheClientConnected && persistentGacheClient.connected()) {
    persistentGacheClient.println(message);
    Serial.println("[TCP] Commande envoyée à la gâche: " + message);
  }
}

void connectToMqtt() {
  if (strlen(userConfig.serverIp) == 0) {
    Serial.println("⚠️ Pas de serveur MQTT configuré");
    return;
  }
  
  mqttClient.setServer(userConfig.serverIp, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  
  String clientId = "CRSD_CONTROLLER_" + String(ESP.getEfuseMac(), HEX);
  
  Serial.print("Connexion MQTT...");
  
  if (mqttClient.connect(clientId.c_str(), userConfig.userEmail, userConfig.userPass)) {
    Serial.println(" ✅ Connecté!");
    
    // S'abonner aux topics de commande gâche
    String gacheCommandTopic = "crsd/+/command";
    mqttClient.subscribe(gacheCommandTopic.c_str());
    Serial.println("Abonné à: " + gacheCommandTopic);
    
    // Publier que le controller est en ligne
    String statusTopic = "crsd/controller/status";
    String statusMsg = "{\"online\":true,\"ip\":\"" + WiFi.localIP().toString() + "\"}";
    mqttClient.publish(statusTopic.c_str(), statusMsg.c_str());
  } else {
    Serial.print(" ❌ Échec (");
    Serial.print(mqttClient.state());
    Serial.println(")");
  }
}

void publishGacheState() {
  if (gacheDeviceId.length() == 0) return;
  
  String topic = "crsd/" + gacheDeviceId + "/state";
  String msg = "{\"state\":\"" + gacheState + "\",\"timestamp\":" + String(millis()) + "}";
  
  if (mqttClient.connected()) {
    mqttClient.publish(topic.c_str(), msg.c_str());
    Serial.println("[MQTT] État gâche publié: " + topic + " = " + msg);
  }
}

void handleGacheTcpClient() {
  // Accepter nouvelle connexion si pas déjà connecté
  if (!gacheClientConnected || !persistentGacheClient.connected()) {
    WiFiClient newClient = gacheTcpServer.available();
    
    if (newClient) {
      Serial.println("=== GÂCHE TCP CONNECTÉE ===");
      persistentGacheClient = newClient;
      gacheClientConnected = false;  // Pas encore authentifié
    }
  }
  
  // Gérer la connexion existante
  if (persistentGacheClient && persistentGacheClient.connected()) {
    if (persistentGacheClient.available()) {
      String cmd = persistentGacheClient.readStringUntil('\n');
      cmd.trim();
      Serial.print("[TCP Gâche] "); Serial.println(cmd);
      
      if (!gacheClientConnected) {
        // Authentification
        if (cmd.startsWith("AUTH:")) {
          String token = cmd.substring(5);
          if (token == "GACHE_SECRET_2024_XyZ9") {
            gacheClientConnected = true;
            persistentGacheClient.println("AUTH_OK");
            Serial.println("✓ Gâche authentifiée");
          } else {
            persistentGacheClient.println("AUTH_FAILED");
            persistentGacheClient.stop();
            gacheClientConnected = false;
          }
        } else {
          persistentGacheClient.println("AUTH_REQUIRED");
        }
        return;
      }
      
      // Gâche authentifiée - traiter les commandes
      if (cmd.startsWith("REGISTER_GACHE:")) {
        String deviceId = cmd.substring(15);
        gacheDeviceId = deviceId;
        Serial.println("✓ Gâche enregistrée: " + deviceId);
        
        // S'abonner aux commandes pour cette gâche
        if (mqttClient.connected()) {
          String topic = "crsd/" + deviceId + "/command";
          mqttClient.subscribe(topic.c_str());
          Serial.println("Abonné MQTT: " + topic);
        }
      }
      else if (cmd.startsWith("STATE:")) {
        gacheState = cmd.substring(6);
        lastGacheStateUpdate = millis();
        Serial.println("État gâche: " + gacheState);
        publishGacheState();
      }
      else if (cmd.startsWith("STATE_JSON:")) {
        // Recevoir JSON complet et relayer vers MQTT
        String json = cmd.substring(11);
        if (mqttClient.connected() && gacheDeviceId.length() > 0) {
          String topic = "crsd/" + gacheDeviceId + "/state";
          mqttClient.publish(topic.c_str(), json.c_str());
          Serial.println("[MQTT] Relayé: " + topic);
        }
      }
      else if (cmd == "PING") {
        persistentGacheClient.println("PONG");
      }
    }
  } else if (gacheClientConnected) {
    // Connexion perdue
    Serial.println("=== GÂCHE TCP DÉCONNECTÉE ===");
    gacheClientConnected = false;
    gacheDeviceId = "";
  }
}

void handleWifiList() {
  int n = WiFi.scanNetworks();
  String json = "{\"networks\":[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"encrypted\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
  }
  json += "]}";
  WiFi.scanDelete();
  server.send(200, "application/json", json);
}

void handleConfig() {
  String body = server.arg(0);
  DynamicJsonDocument doc(512);
  DeserializationError err = deserializeJson(doc, body);
  
  if (err) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"JSON invalide\"}");
    return;
  }
  
  strlcpy(userConfig.wifiSsid, doc["ssid"] | "", sizeof(userConfig.wifiSsid));
  strlcpy(userConfig.wifiPass, doc["pass"] | "", sizeof(userConfig.wifiPass));
  strlcpy(userConfig.serverIp, doc["serverIp"] | "", sizeof(userConfig.serverIp));
  userConfig.serverPort = doc["serverPort"] | 8000;
  strlcpy(userConfig.userEmail, doc["email"] | "", sizeof(userConfig.userEmail));
  strlcpy(userConfig.userPass, doc["passUser"] | "", sizeof(userConfig.userPass));
  
  prefs.putString("ssid", userConfig.wifiSsid);
  prefs.putString("pass", userConfig.wifiPass);
  prefs.putString("serverIp", userConfig.serverIp);
  prefs.putInt("port", userConfig.serverPort);
  prefs.putString("email", userConfig.userEmail);
  prefs.putString("userPass", userConfig.userPass);
  
  Serial.println("Config saved to flash");
  server.send(200, "application/json", "{\"success\":true}");
  
  currentState = CONNECTING_WIFI;
}

void startNormalMode() {
  Serial.println("Mode NORMAL activé");
  
  strlcpy(userConfig.wifiSsid, prefs.getString("ssid", "").c_str(), sizeof(userConfig.wifiSsid));
  strlcpy(userConfig.wifiPass, prefs.getString("pass", "").c_str(), sizeof(userConfig.wifiPass));
  strlcpy(userConfig.serverIp, prefs.getString("serverIp", "").c_str(), sizeof(userConfig.serverIp));
  userConfig.serverPort = prefs.getInt("port", 8000);
  strlcpy(userConfig.userEmail, prefs.getString("email", "").c_str(), sizeof(userConfig.userEmail));
  strlcpy(userConfig.userPass, prefs.getString("userPass", "").c_str(), sizeof(userConfig.userPass));
  
  Serial.print("WiFi: "); Serial.println(userConfig.wifiSsid);
  Serial.print("Server: "); Serial.print(userConfig.serverIp); Serial.print(":"); Serial.println(userConfig.serverPort);
  
  // Démarrer le serveur TCP pour la gâche
  gacheTcpServer.begin();
  Serial.println("Serveur TCP gâche démarré sur port 5003");
  
  connectToWifi();
}

void connectToWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(userConfig.wifiSsid, userConfig.wifiPass);
  
  Serial.print("Connexion");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connecté!");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    
    digitalWrite(LED_PIN, HIGH);
    
    // Démarrer le serveur TCP pour la gâche
    gacheTcpServer.begin();
    Serial.println("Serveur TCP gâche démarré sur port 5003");
    
    currentState = DISCOVERING_CAM;
    discoverAndConfigCam();
  } else {
    Serial.println("\nÉchec WiFi - retour mode setup");
    currentState = SETUP_MODE;
    prefs.clear();
    ESP.restart();
  }
}

void discoverAndConfigCam() {
  Serial.println("=== DÉCOUVERTE ET CONFIGURATION CAMÉRA ===");
  
  // Scanner les AP pour trouver la caméra
  Serial.println("Scan des AP caméra...");
  int n = WiFi.scanNetworks();
  
  String camSsid = "";
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.startsWith("CRSD-CAM-")) {
      camSsid = ssid;
      Serial.println("🎯 Caméra trouvée: " + ssid);
      break;
    }
  }
  WiFi.scanDelete();
  
  if (camSsid.length() == 0) {
    Serial.println("❌ Aucune caméra trouvée (AP CRSD-CAM-* non détecté)");
    Serial.println("⚠️ Vérifiez que la caméra est branchée et en mode AP");
    
    // Continuer avec les autres équipements même sans caméra
    registerUserOnServer();
    discoverAndConfigDevices();
    return;
  }
  
  // Se connecter temporairement au WiFi caméra
  Serial.println("Connexion au WiFi caméra...");
  WiFi.begin(camSsid.c_str(), "");
  
  unsigned long timeout = millis() + 10000;
  while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n❌ Échec connexion WiFi caméra");
    return;
  }
  
  Serial.println("\n✅ Connecté au WiFi caméra!");
  Serial.print("IP caméra: "); Serial.println(WiFi.localIP().toString());
  
  // Configurer la caméra
  configureCamera("192.168.50.1");
  
  // Se reconnecter au WiFi maison
  Serial.println("\nReconnexion au WiFi maison...");
  WiFi.begin(userConfig.wifiSsid, userConfig.wifiPass);
  
  timeout = millis() + 15000;
  while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Reconnecté au WiFi maison!");
    Serial.print("IP: "); Serial.println(WiFi.localIP().toString());
    
    // Enregistrer l'utilisateur
    registerUserOnServer();
    
    // Découvrir et configurer autres équipements
    discoverAndConfigDevices();
  }
}

void configureCamera(String camIp) {
  Serial.println("Configuration caméra @ " + camIp);
  
  WiFiClient camClient;
  if (!camClient.connect(camIp.c_str(), 5000)) {
    Serial.println("❌ Connexion TCP échouée");
    return;
  }
  
  Serial.println("✅ TCP connecté - Authentification...");
  camClient.println(String("AUTH:") + AUTH_TOKEN);
  
  unsigned long timeout = millis() + 5000;
  bool authOk = false;
  
  while (millis() < timeout) {
    if (camClient.available()) {
      String resp = camClient.readStringUntil('\n');
      resp.trim();
      if (resp == "AUTH_OK") {
        authOk = true;
        break;
      }
    }
    delay(10);
  }
  
  if (!authOk) {
    Serial.println("❌ Authentification échouée");
    camClient.stop();
    return;
  }
  
  Serial.println("✅ Authentifié - Configuration serveur...");
  String configCmd = "SET_SERVER:" + String(userConfig.serverIp) + ":" + String(userConfig.serverPort);
  camClient.println(configCmd);
  
  timeout = millis() + 5000;
  bool ack = false;
  
  while (millis() < timeout) {
    if (camClient.available()) {
      String resp = camClient.readStringUntil('\n');
      if (resp.indexOf("ACK") >= 0) {
        ack = true;
        break;
      }
    }
    delay(10);
  }
  
  if (!ack) {
    Serial.println("❌ Pas d'ACK serveur");
    camClient.stop();
    return;
  }
  
  Serial.println("✅ Serveur configuré - Configuration WiFi...");
  String wifiCmd = "SET_WIFI:" + String(userConfig.wifiSsid) + ":" + String(userConfig.wifiPass);
  camClient.println(wifiCmd);
  
  timeout = millis() + 5000;
  bool wifiAck = false;
  
  while (millis() < timeout) {
    if (camClient.available()) {
      String resp = camClient.readStringUntil('\n');
      if (resp.indexOf("WIFI_ACK") >= 0) {
        wifiAck = true;
        break;
      }
    }
    delay(10);
  }
  
  if (!wifiAck) {
    Serial.println("❌ Pas d'ACK WiFi");
    camClient.stop();
    return;
  }
  
  Serial.println("✅ WiFi configuré - Redémarrage caméra...");
  camClient.println("RESTART");
  
  timeout = millis() + 3000;
  while (millis() < timeout) {
    if (camClient.available()) {
      String resp = camClient.readStringUntil('\n');
      if (resp.indexOf("RESTART_ACK") >= 0) {
        break;
      }
    }
    delay(10);
  }
  
  camClient.stop();
  Serial.println("✅ Caméra configurée avec succès!");
  blinkLed(200, 3);
}

void registerUserOnServer() {
  Serial.println("=== ENREGISTREMENT UTILISATEUR SUR SERVEUR ===");
  
  WiFiClient serverClient;
  // Utiliser le port HTTP (5001) - le serveur Python écoute sur 5001
  int httpPort = 5001;
  Serial.print("Connexion au serveur: ");
  Serial.print(userConfig.serverIp); Serial.print(":"); Serial.println(httpPort);
  
  if (!serverClient.connect(userConfig.serverIp, httpPort)) {
    Serial.println("❌ Connexion serveur échouée");
    return;
  }
  
  Serial.println("✅ Connecté au serveur");
  
  // Envoyer requête HTTP POST au lieu de TCP brut
  String postData = "email=" + String(userConfig.userEmail) + "&password=" + String(userConfig.userPass);
  
  serverClient.println("POST /register HTTP/1.1");
  serverClient.println("Host: " + String(userConfig.serverIp));
  serverClient.println("Content-Type: application/x-www-form-urlencoded");
  serverClient.println("Content-Length: " + String(postData.length()));
  serverClient.println("Connection: close");
  serverClient.println();
  serverClient.println(postData);
  
  Serial.println("Envoi: POST /register avec email=" + String(userConfig.userEmail));
  
  // Attendre réponse HTTP
  unsigned long timeout = millis() + 10000;
  String response = "";
  
  while (millis() < timeout && serverClient.connected()) {
    if (serverClient.available()) {
      response += serverClient.readString();
      break;
    }
    delay(10);
  }
  
  serverClient.stop();
  
  if (response.indexOf("200 OK") >= 0) {
    Serial.println("✅ Utilisateur enregistré sur le serveur");
  } else if (response.indexOf("USER_EXISTS") >= 0) {
    Serial.println("✅ Utilisateur déjà existant sur le serveur");
  } else {
    Serial.println("❌ Erreur enregistrement utilisateur");
    Serial.println("Réponse: " + response.substring(0, 200));
  }
}

void discoverAndConfigDevices() {
  Serial.println("\n=== DÉCOUVERTE ÉQUIPEMENTS SUPPLÉMENTAIRES ===");
  
  // Découvrir gâche électrique
  discoverAndConfigGache();
  
  // Ici on peut ajouter d'autres équipements :
  // discoverAndConfigCapteurs();
  // discoverAndConfigEclairage();
}

void discoverAndConfigGache() {
  Serial.println("Recherche gâche électrique...");
  
  // Sauvegarder l'IP du WiFi maison AVANT de changer de réseau
  homeWifiIp = WiFi.localIP().toString();
  Serial.println("IP WiFi maison sauvegardée: " + homeWifiIp);
  
  WiFiUDP udp;
  udp.begin(5002);
  
  const char* discoverMsg = "CRSD_DISCOVER";
  IPAddress broadcastIP = WiFi.localIP();
  broadcastIP[3] = 255;
  
  udp.beginPacket(broadcastIP, 5002);
  udp.write((const uint8_t*)discoverMsg, strlen(discoverMsg));
  udp.endPacket();
  
  String gacheIp = "";
  unsigned long timeout = millis() + 3000;
  
  while (millis() < timeout) {
    int packetSize = udp.parsePacket();
    if (packetSize) {
      char buf[256];
      int len = udp.read(buf, sizeof(buf) - 1);
      buf[len] = '\0';
      
      if (String(buf) == "CRSD_GACHE_FOUND") {
        IPAddress remoteIP = udp.remoteIP();
        gacheIp = remoteIP.toString();
        Serial.println("🔓 Gâche trouvée: " + gacheIp);
        break;
      }
    }
    delay(10);
  }
  udp.stop();
  
  if (gacheIp.length() == 0) {
    // Scanner les AP gâche
    Serial.println("Scan des AP gâche...");
    int n = WiFi.scanNetworks();
    
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      
      if (ssid.startsWith("CRSD-GACHE-")) {
        Serial.println("🎯 GÂCHE TROUVÉE: " + ssid);
        
        // Se connecter temporairement
        WiFi.begin(ssid.c_str(), "");
        
        unsigned long timeout = millis() + 10000;
        while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
          delay(500);
          Serial.print(".");
        }
        
        if (WiFi.status() == WL_CONNECTED) {
          Serial.println("\n✅ Connecté au WiFi gâche!");
          
          gacheIp = "192.168.60.1";
          configureGache(gacheIp);
          
          // Reconnecter au WiFi maison
          Serial.println("Reconnexion WiFi maison...");
          WiFi.begin(userConfig.wifiSsid, userConfig.wifiPass);
          
          timeout = millis() + 15000;
          while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
            delay(500);
            Serial.print(".");
          }
          
          if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\n✅ Reconnecté au WiFi maison!");
          }
          return;
        }
      }
    }
    WiFi.scanDelete();
  } else {
    // Gâche déjà sur le réseau
    configureGache(gacheIp);
  }
}

void configureGache(String gacheIp) {
  Serial.println("Configuration gâche @ " + gacheIp);
  
  WiFiClient gacheClient;
  if (!gacheClient.connect(gacheIp.c_str(), 5001)) {
    Serial.println("❌ Connexion TCP gâche échouée");
    return;
  }
  
  Serial.println("✅ TCP connecté - Authentification...");
  gacheClient.println("AUTH:GACHE_SECRET_2024_XyZ9");
  
  unsigned long timeout = millis() + 5000;
  bool authOk = false;
  
  while (millis() < timeout) {
    if (gacheClient.available()) {
      String resp = gacheClient.readStringUntil('\n');
      resp.trim();
      if (resp == "AUTH_OK") {
        authOk = true;
        break;
      }
    }
    delay(10);
  }
  
  if (!authOk) {
    Serial.println("❌ Authentification gâche échouée");
    gacheClient.stop();
    return;
  }
  
  Serial.println("✅ Authentifié - Configuration Controller...");
  
  // Générer ID unique pour la gâche
  uint32_t chipId = ESP.getEfuseMac();
  String deviceId = "gache_" + String(chipId, HEX);
  
  // Configuration Controller : utiliser l'IP du WiFi maison (pas l'IP temporaire)
  // La gâche se connectera en TCP sur ce port
  String controllerCmd = "SET_CONTROLLER:" + homeWifiIp + 
                         ":5003:" + deviceId;
  
  Serial.println("Envoi config Controller...");
  gacheClient.println(controllerCmd);
  
  timeout = millis() + 5000;
  bool controllerAck = false;
  
  while (millis() < timeout) {
    if (gacheClient.available()) {
      String resp = gacheClient.readStringUntil('\n');
      if (resp.indexOf("CONTROLLER_ACK") >= 0) {
        controllerAck = true;
        break;
      }
    }
    delay(10);
  }
  
  if (!controllerAck) {
    Serial.println("❌ Pas d'ACK Controller");
    gacheClient.stop();
    return;
  }
  
  Serial.println("✅ Controller configuré - Configuration WiFi...");
  String wifiCmd = "SET_WIFI:" + String(userConfig.wifiSsid) + ":" + String(userConfig.wifiPass);
  gacheClient.println(wifiCmd);
  
  timeout = millis() + 5000;
  bool wifiAck = false;
  
  while (millis() < timeout) {
    if (gacheClient.available()) {
      String resp = gacheClient.readStringUntil('\n');
      if (resp.indexOf("WIFI_ACK") >= 0) {
        wifiAck = true;
        break;
      }
    }
    delay(10);
  }
  
  if (!wifiAck) {
    Serial.println("❌ Pas d'ACK WiFi gâche");
    gacheClient.stop();
    return;
  }
  
  Serial.println("✅ WiFi gâche configuré - Redémarrage...");
  gacheClient.println("RESTART");
  
  timeout = millis() + 3000;
  while (millis() < timeout) {
    if (gacheClient.available()) {
      String resp = gacheClient.readStringUntil('\n');
      if (resp.indexOf("RESTART_ACK") >= 0) {
        break;
      }
    }
    delay(10);
  }
  
  gacheClient.stop();
  Serial.println("✅ Gâche configurée avec succès!");
  Serial.println("Device ID: " + deviceId);
  Serial.println("La gâche se connectera en TCP sur: " + homeWifiIp + ":5003");
}

void loop() {
  if (currentState == SETUP_MODE) {
    dnsServer.processNextRequest();
    server.handleClient();
  } else if (currentState == CONNECTING_WIFI) {
    connectToWifi();
    currentState = RUNNING;
  } else if (currentState == DISCOVERING_CAM) {
    discoverAndConfigCam();
    currentState = RUNNING;
  } else if (currentState == RUNNING) {
    // Gestion MQTT
    if (!mqttClient.connected()) {
      connectToMqtt();
    }
    mqttClient.loop();
    
    // Gestion connexions TCP gâche
    handleGacheTcpClient();
    
    // Heartbeat
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 60000) {
      lastCheck = millis();
      Serial.println("Heartbeat - WiFi: " + WiFi.localIP().toString());
      Serial.println("Gâche: " + gacheDeviceId + " - " + gacheState);
    }
  }
  
  delay(10);
}
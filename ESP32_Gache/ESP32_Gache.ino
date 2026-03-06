#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// ==================== MODE AP ====================
const char* AP_SSID = "CRSD-GACHE-";
const char* AP_PASSWORD = "";
const IPAddress AP_IP(192, 168, 60, 1);
const IPAddress AP_GATEWAY(192, 168, 60, 1);
const IPAddress AP_SUBNET(255, 255, 255, 0);

bool apMode = false;

// ==================== HARDWARE ====================
#define RELAY_PIN 15        // Relais gâche
#define STATUS_LED_PIN 2    // LED status
#define BUTTON_PIN 0        // Bouton manuel

// ==================== SECURITY ====================
const char* AUTH_TOKEN = "GACHE_SECRET_2024_XyZ9";

// ==================== NETWORK ====================
WiFiClient controllerClient;   // Connexion TCP vers Controller
WiFiClient commandClient;       // Client TCP pour config locale
WiFiServer commandServer(5001); // Serveur TCP pour config locale
WiFiUDP udpDiscovery;

const int DISCOVERY_PORT = 5002;
const int COMMAND_PORT = 5001;
const int CONTROLLER_PORT = 5003; // Port pour communiquer avec Controller

Preferences prefs;
bool authenticated = false;

// ==================== CONFIG ====================
struct Config {
  char wifiSsid[64] = "";
  char wifiPass[64] = "";
  char controllerIp[64] = "";  // IP du Controller
  int controllerPort = 5003;
  char deviceId[32] = "";
} gacheConfig;

// ==================== ÉTAT ====================
bool gacheState = false;  // false = fermée, true = ouverte
bool buttonPressed = false;
unsigned long lastStatePublish = 0;
unsigned long lastButtonCheck = 0;
bool controllerConnected = false;

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== CRSD Gâche v1.0.0 ===");
  
  // Configuration pins
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // Sécurité : gâche fermée au démarrage
  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(STATUS_LED_PIN, LOW);
  gacheState = false;
  
  prefs.begin("gacheConfig", false);
  
  // Vérifier si c'est un redémarrage après configuration
  bool isConfigRestart = prefs.getBool("configRestart", false);
  
  if (!isConfigRestart) {
    // Premier démarrage après téléversement → Effacer config
    prefs.clear();
    Serial.println("🔄 Config gâche effacée automatiquement (téléversement)");
  } else {
    // Redémarrage après configuration → Garder config
    prefs.putBool("configRestart", false);
    Serial.println("🔄 Redémarrage après configuration - Config conservée");
  }

  // Charger config
  String savedSsid = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  
  if (savedSsid.length() == 0) {
    // Mode AP pour configuration
    Serial.println("Mode SETUP - Pas de WiFi, création AP...");
    apMode = true;
    startApMode();
  } else {
    // Mode normal - connexion WiFi
    Serial.println("Mode NORMAL - Connexion WiFi...");
    loadConfig();
    connectToWifi();
  }
}

void startApMode() {
  WiFi.mode(WIFI_AP);
  uint32_t chipId = ESP.getEfuseMac();
  String apSsid = String(AP_SSID) + String(chipId, HEX);
  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
  WiFi.softAP(apSsid.c_str(), AP_PASSWORD);
  
  Serial.println("AP: " + apSsid);
  Serial.println("IP: " + WiFi.softAPIP().toString());
  
  // Démarrer services
  udpDiscovery.begin(DISCOVERY_PORT);
  commandServer.begin();
  
  Serial.println("UDP Discovery started on port 5002");
  Serial.println("TCP Command Server started on port 5001");
  
  // LED clignotante en mode setup
  blinkStatusLed(3);
}

void loadConfig() {
  strlcpy(gacheConfig.wifiSsid, prefs.getString("ssid", "").c_str(), sizeof(gacheConfig.wifiSsid));
  strlcpy(gacheConfig.wifiPass, prefs.getString("pass", "").c_str(), sizeof(gacheConfig.wifiPass));
  strlcpy(gacheConfig.controllerIp, prefs.getString("controllerIp", "").c_str(), sizeof(gacheConfig.controllerIp));
  gacheConfig.controllerPort = prefs.getInt("controllerPort", 5003);
  strlcpy(gacheConfig.deviceId, prefs.getString("deviceId", "").c_str(), sizeof(gacheConfig.deviceId));
}

void connectToWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(gacheConfig.wifiSsid, gacheConfig.wifiPass);
  
  Serial.print("Connexion WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connecté!");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    
    // LED fixe = connecté
    digitalWrite(STATUS_LED_PIN, HIGH);
    
    // Démarrer services
    udpDiscovery.begin(DISCOVERY_PORT);
    commandServer.begin();
    
    // Connexion au Controller
    connectToController();
    
    Serial.println("✅ Gâche opérationnelle");
  } else {
    Serial.println("\n❌ Échec WiFi - mode AP");
    apMode = true;
    startApMode();
  }
}

// ==================== TCP VERS CONTROLLER ====================
void connectToController() {
  if (strlen(gacheConfig.controllerIp) == 0) {
    Serial.println("⚠️ Pas d'IP Controller configurée");
    return;
  }
  
  Serial.print("Connexion TCP au Controller (");
  Serial.print(gacheConfig.controllerIp);
  Serial.print(":");
  Serial.print(gacheConfig.controllerPort);
  Serial.print(")...");
  
  if (controllerClient.connect(gacheConfig.controllerIp, gacheConfig.controllerPort)) {
    Serial.println(" ✅ Connecté!");
    controllerConnected = true;
    
    // S'authentifier
    controllerClient.println(String("AUTH:") + AUTH_TOKEN);
    
    unsigned long timeout = millis() + 5000;
    while (millis() < timeout) {
      if (controllerClient.available()) {
        String resp = controllerClient.readStringUntil('\n');
        resp.trim();
        if (resp == "AUTH_OK") {
          Serial.println("✓ Authentifié auprès du Controller");
          // Envoyer l'ID de la gâche
          controllerClient.println("REGISTER_GACHE:" + String(gacheConfig.deviceId));
          break;
        }
      }
      delay(10);
    }
  } else {
    Serial.println(" ❌ Échec");
    controllerConnected = false;
  }
}

void sendStateToController() {
  if (!controllerConnected || !controllerClient.connected()) {
    return;
  }
  
  String stateMsg = "STATE:" + String(gacheState ? "open" : "closed");
  controllerClient.println(stateMsg);
}

void handleControllerCommand(String cmd) {
  Serial.println("Commande Controller: " + cmd);
  
  if (cmd == "OPEN") {
    openGache();
  } else if (cmd == "CLOSE") {
    closeGache();
  } else if (cmd == "TOGGLE") {
    toggleGache();
  } else if (cmd == "STATUS") {
    sendStateToController();
  }
}

void openGache() {
  Serial.println("🔓 Ouverture gâche");
  digitalWrite(RELAY_PIN, LOW);  // Activer relais
  gacheState = true;
  publishState();
  
  // Auto-fermeture après 3 secondes (sécurité)
  delay(3000);
  closeGache();
}

void closeGache() {
  Serial.println("🔒 Fermeture gâche");
  digitalWrite(RELAY_PIN, HIGH);  // Désactiver relais
  gacheState = false;
  publishState();
}

void toggleGache() {
  if (gacheState) {
    closeGache();
  } else {
    openGache();
  }
}

void publishState() {
  // Envoyer l'état au Controller via TCP
  if (controllerConnected && controllerClient.connected()) {
    String stateJson = "{\"state\":\"" + String(gacheState ? "open" : "closed") + 
                       "\",\"timestamp\":" + String(millis()) + 
                       ",\"deviceId\":\"" + String(gacheConfig.deviceId) + "\"}";
    controllerClient.println("STATE_JSON:" + stateJson);
  }
}

void blinkStatusLed(int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(STATUS_LED_PIN, HIGH);
    delay(200);
    digitalWrite(STATUS_LED_PIN, LOW);
    delay(200);
  }
}

void loop() {
  // Gestion UDP Discovery
  int packetSize = udpDiscovery.parsePacket();
  if (packetSize) {
    char incomingPacket[256];
    int len = udpDiscovery.read(incomingPacket, sizeof(incomingPacket) - 1);
    incomingPacket[len] = '\0';
    
    if (strcmp(incomingPacket, "CRSD_DISCOVER") == 0) {
      Serial.println("Reçu demande de découverte UDP");
      udpDiscovery.beginPacket(udpDiscovery.remoteIP(), DISCOVERY_PORT);
      const char* response = "CRSD_GACHE_FOUND";
      udpDiscovery.write((const uint8_t*)response, strlen(response));
      udpDiscovery.endPacket();
    }
  }
  
  // Gestion commandes TCP
  if (!commandClient.connected()) {
    commandClient = commandServer.available();
    if (commandClient) {
      Serial.println("=== CLIENT TCP CONNECTÉ ===");
      authenticated = false;
    }
  } else {
    if (commandClient.available()) {
      String cmd = commandClient.readStringUntil('\n');
      cmd.trim();
      Serial.print("Commande reçue: "); Serial.println(cmd);
      
      if (!authenticated) {
        if (cmd.startsWith("AUTH:")) {
          String token = cmd.substring(5);
          if (token == AUTH_TOKEN) {
            authenticated = true;
            commandClient.println("AUTH_OK");
            Serial.println("✓ Authentification réussie");
          } else {
            commandClient.println("AUTH_FAILED");
            Serial.println("✗ Authentification échouée");
            commandClient.stop();
          }
        } else {
          commandClient.println("AUTH_REQUIRED");
          commandClient.stop();
        }
        return;
      }
      
      // Commandes authentifiées
      if (cmd.startsWith("SET_CONTROLLER:")) {
        handleControllerConfig(cmd);
      } else if (cmd.startsWith("SET_WIFI:")) {
        handleWifiConfig(cmd);
      } else if (cmd == "RESTART") {
        Serial.println("=== REDÉMARRAGE DEMANDÉ ===");
        commandClient.println("RESTART_ACK");
        commandClient.stop();
        prefs.putBool("configRestart", true);
        delay(500);
        ESP.restart();
      } else if (cmd == "OPEN") {
        openGache();
        commandClient.println("OK");
      } else if (cmd == "CLOSE") {
        closeGache();
        commandClient.println("OK");
      } else {
        commandClient.println("UNKNOWN_COMMAND");
      }
    }
  }
  
  // Bouton manuel
  if (millis() - lastButtonCheck > 50) {
    bool currentButton = digitalRead(BUTTON_PIN) == LOW;
    if (currentButton && !buttonPressed) {
      Serial.println("🔘 Bouton manuel pressé");
      toggleGache();
    }
    buttonPressed = currentButton;
    lastButtonCheck = millis();
  }
  
  // Gestion connexion TCP vers Controller
  if (!apMode && !controllerConnected) {
    static unsigned long lastConnectAttempt = 0;
    if (millis() - lastConnectAttempt > 10000) {  // Réessayer toutes les 10s
      connectToController();
      lastConnectAttempt = millis();
    }
  }
  
  // Vérifier si la connexion est toujours active
  if (controllerConnected && !controllerClient.connected()) {
    Serial.println("⚠️ Connexion Controller perdue");
    controllerConnected = false;
  }
  
  // Recevoir commandes du Controller
  if (controllerConnected && controllerClient.connected() && controllerClient.available()) {
    String cmd = controllerClient.readStringUntil('\n');
    cmd.trim();
    handleControllerCommand(cmd);
  }
  
  // Envoyer état périodiquement au Controller
  if (!apMode && controllerConnected && millis() - lastStatePublish > 30000) {
    sendStateToController();
    lastStatePublish = millis();
  }
  
  delay(10);
}

void handleControllerConfig(String cmd) {
  // Format: SET_CONTROLLER:ip:port:deviceId
  cmd.replace("SET_CONTROLLER:", "");
  
  int sep1 = cmd.indexOf(':');
  int sep2 = cmd.indexOf(':', sep1 + 1);
  
  if (sep1 > 0 && sep2 > 0) {
    String controllerIp = cmd.substring(0, sep1);
    int controllerPort = cmd.substring(sep1 + 1, sep2).toInt();
    String deviceId = cmd.substring(sep2 + 1);
    
    prefs.putString("controllerIp", controllerIp);
    prefs.putInt("controllerPort", controllerPort);
    prefs.putString("deviceId", deviceId);
    
    // Charger la nouvelle config
    loadConfig();
    
    Serial.println("Controller configuré: " + controllerIp + ":" + String(controllerPort));
    Serial.println("Device ID: " + deviceId);
    commandClient.println("CONTROLLER_ACK");
    
    // Se connecter au Controller
    connectToController();
  } else {
    Serial.println("Format SET_CONTROLLER invalide");
    commandClient.println("ERROR");
  }
}

void handleWifiConfig(String cmd) {
  // Format: SET_WIFI:ssid:password
  cmd.replace("SET_WIFI:", "");
  int sep = cmd.indexOf(':');
  
  if (sep > 0) {
    String ssid = cmd.substring(0, sep);
    String pass = cmd.substring(sep + 1);
    
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    
    Serial.println("WiFi configuré: " + ssid);
    commandClient.println("WIFI_ACK");
  } else {
    Serial.println("Format SET_WIFI invalide");
    commandClient.println("ERROR");
  }
}
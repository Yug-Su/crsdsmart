#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>

// ==================== MODE AP ====================
const char* AP_SSID = "CRSD-CAM-";
const char* AP_PASSWORD = "";
const IPAddress AP_IP(192, 168, 50, 1);
const IPAddress AP_GATEWAY(192, 168, 50, 1);
const IPAddress AP_SUBNET(255, 255, 255, 0);

bool apMode = false;

// ===========================
// CAMERA MODEL
// ===========================
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

// ===========================
// SECURITY TOKEN
// ===========================
const char *AUTH_TOKEN = "CAM_SECRET_2024_XyZ9";

// ===========================
// TCP SERVERS
// ===========================
WiFiServer commandServer(5000);
Preferences prefs;

String serverIP = "";
int serverPort = 0;
WiFiClient videoClient;
WiFiClient commandClient;
bool authenticated = false;

// UDP pour découverte
WiFiUDP udpDiscovery;
const int DISCOVERY_PORT = 5001;
const int COMMAND_PORT = 5000;

unsigned long lastSend = 0;
const int sendIntervalMs = 100;

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  prefs.begin("camConfig", false);
  
  // Vérifier si c'est un redémarrage après configuration
  bool isConfigRestart = prefs.getBool("configRestart", false);
  
  if (!isConfigRestart) {
    // Premier démarrage après téléversement → Effacer config
    prefs.clear();
    Serial.println("🔄 Config caméra effacée automatiquement (téléversement)");
  } else {
    // Redémarrage après configuration → Garder config
    prefs.putBool("configRestart", false);  // Reset flag
    Serial.println("🔄 Redémarrage après configuration - Config conservée");
  }

  // Charger config WiFi
  String savedSsid = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  
  if (savedSsid.length() == 0) {
    // Pas de WiFi configuré → Mode AP pour être découverte
    Serial.println("Mode SETUP - Pas de WiFi, création AP...");
    apMode = true;
    WiFi.mode(WIFI_AP);
    uint32_t chipId = ESP.getEfuseMac();
    String apSsid = String(AP_SSID) + String(chipId, HEX);
    WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
    WiFi.softAP(apSsid.c_str(), AP_PASSWORD);
    Serial.println("AP: " + apSsid);
    Serial.println("IP: " + WiFi.softAPIP().toString());
    
    // Démarrer UDP pour découverte
    udpDiscovery.begin(DISCOVERY_PORT);
    Serial.println("UDP Discovery started on port 5001");
    
    // Démarrer serveur TCP pour configuration
    commandServer.begin();
    Serial.println("TCP Command Server started on port 5000");
    
    return;  // Sortir du setup - pas d'initialisation caméra
  } else {
    // Connexion au WiFi
    Serial.print("Connexion WiFi: "); Serial.println(savedSsid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSsid.c_str(), savedPass.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connecté!");
      Serial.print("IP: "); Serial.println(WiFi.localIP());
    } else {
      Serial.println("\nÉchec WiFi - mode AP");
      apMode = true;
      WiFi.mode(WIFI_AP);
      uint32_t chipId = ESP.getEfuseMac();
      String apSsid = String(AP_SSID) + String(chipId, HEX);
      WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
      WiFi.softAP(apSsid.c_str(), AP_PASSWORD);
      
      udpDiscovery.begin(DISCOVERY_PORT);
      commandServer.begin();
      Serial.println("Fallback AP mode");
      return;
    }
  }

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_QVGA;  // Plus petit pour éviter erreur mémoire
    config.jpeg_quality = 15;
    config.fb_count = 1;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 20;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_QVGA);

  WiFi.setSleep(false);

  serverIP = prefs.getString("ip", "");
  serverPort = prefs.getInt("port", 0);
  if (serverIP != "" && serverPort != 0) {
    Serial.println("Config loaded:");
    Serial.print("Server: "); Serial.println(serverIP);
    Serial.print("Port: "); Serial.println(serverPort);
  }

  commandServer.begin();
  Serial.println("TCP Command Server started on port 5000 (AUTH required)");
  
  udpDiscovery.begin(DISCOVERY_PORT);
  Serial.println("UDP Discovery started on port 5001");
}

void handleCommand(String cmd) {
  sensor_t *s = esp_camera_sensor_get();
  
  if (cmd.startsWith("framesize:")) {
    int val = cmd.substring(10).toInt();
    s->set_framesize(s, (framesize_t)val);
  } else if (cmd.startsWith("quality:")) {
    s->set_quality(s, cmd.substring(8).toInt());
  } else if (cmd.startsWith("brightness:")) {
    s->set_brightness(s, cmd.substring(11).toInt());
  } else if (cmd.startsWith("contrast:")) {
    s->set_contrast(s, cmd.substring(9).toInt());
  } else if (cmd.startsWith("saturation:")) {
    s->set_saturation(s, cmd.substring(11).toInt());
  } else if (cmd.startsWith("awb:")) {
    s->set_whitebal(s, cmd.substring(4).toInt());
  } else if (cmd.startsWith("agc:")) {
    s->set_gain_ctrl(s, cmd.substring(4).toInt());
  } else if (cmd.startsWith("aec:")) {
    s->set_exposure_ctrl(s, cmd.substring(4).toInt());
  } else if (cmd.startsWith("hmirror:")) {
    s->set_hmirror(s, cmd.substring(8).toInt());
  } else if (cmd.startsWith("vflip:")) {
    s->set_vflip(s, cmd.substring(6).toInt());
  } else if (cmd.startsWith("special_effect:")) {
    s->set_special_effect(s, cmd.substring(15).toInt());
  }
}

void loop() {
  // Gestion UDP Discovery (toujours actif, même en mode AP)
  int packetSize = udpDiscovery.parsePacket();
  if (packetSize) {
    char incomingPacket[256];
    int len = udpDiscovery.read(incomingPacket, sizeof(incomingPacket) - 1);
    incomingPacket[len] = '\0';
    
    if (strcmp(incomingPacket, "CRSD_DISCOVER") == 0) {
      Serial.println("Reçu demande de découverte UDP");
      udpDiscovery.beginPacket(udpDiscovery.remoteIP(), DISCOVERY_PORT);
      const char* response = "CRSD_CAMERA_FOUND";
      udpDiscovery.write((const uint8_t*)response, strlen(response));
      udpDiscovery.endPacket();
    }
  }
  
  // Gestion commandes TCP (même en mode AP pour configuration)
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
      
      // Première commande doit être AUTH
      if (!authenticated) {
        if (cmd.startsWith("AUTH:")) {
          String token = cmd.substring(5);
          Serial.print("Token reçu: "); Serial.println(token);
          if (token == AUTH_TOKEN) {
            authenticated = true;
            commandClient.println("AUTH_OK");
            Serial.println("✓ Authentification réussie");
          } else {
            commandClient.println("AUTH_FAILED");
            Serial.println("✗ Authentification échouée - mauvais token");
            commandClient.stop();
          }
        } else {
          commandClient.println("AUTH_REQUIRED");
          Serial.println("✗ AUTH requis d'abord");
          commandClient.stop();
        }
        return;
      }
      
      // Commandes authentifiées
      Serial.print("CMD authentifiée: "); Serial.println(cmd);
      
      if (cmd.startsWith("CRSD_PING")) {
        Serial.println("Réponse PING");
        commandClient.println("CRSD_CAMERA");
      } else if (cmd.startsWith("SET_SERVER:")) {
        cmd.replace("SET_SERVER:", "");
        int sep = cmd.indexOf(':');
        if (sep > 0) {
          serverIP = cmd.substring(0, sep);
          serverPort = cmd.substring(sep + 1).toInt();
          prefs.putString("ip", serverIP);
          prefs.putInt("port", serverPort);
          Serial.print("Serveur configuré: "); Serial.print(serverIP); Serial.print(":"); Serial.println(serverPort);
          commandClient.println("ACK");
        } else {
          Serial.println("Format SET_SERVER invalide");
          commandClient.println("ERROR");
        }
      } else if (cmd == "RESTART") {
        Serial.println("=== REDÉMARRAGE DEMANDÉ ===");
        commandClient.println("RESTART_ACK");
        commandClient.stop();
        
        // Marquer comme redémarrage de configuration
        prefs.putBool("configRestart", true);
        
        delay(500);
        ESP.restart();
      } else if (cmd.startsWith("SET_WIFI:")) {
        cmd.replace("SET_WIFI:", "");
        int sep = cmd.indexOf(':');
        if (sep > 0) {
          String newSsid = cmd.substring(0, sep);
          String newPass = cmd.substring(sep + 1);
          
          Serial.print("Nouveau WiFi: "); Serial.print(newSsid); Serial.println(" (pass: ***)");
          
          prefs.putString("ssid", newSsid);
          prefs.putString("pass", newPass);
          
          commandClient.println("WIFI_ACK");
          Serial.println("WiFi sauvegardé!");
        } else {
          Serial.println("Format SET_WIFI invalide");
          commandClient.println("ERROR");
        }
      } else {
        handleCommand(cmd);
        commandClient.println("OK");
      }
    }
  }

  // Envoi vidéo TCP (seulement si pas en mode AP et serveur configuré)
  if (!apMode && serverIP != "" && serverPort != 0) {
    if (!videoClient.connected()) {
      Serial.print("Connecting to video server...");
      if (videoClient.connect(serverIP.c_str(), serverPort)) {
        Serial.println("connected!");
      } else {
        Serial.println("failed");
        delay(2000);
        return;
      }
    }

    if (millis() - lastSend >= sendIntervalMs) {
      camera_fb_t *fb = esp_camera_fb_get();
      if (fb) {
        uint32_t len = fb->len;
        videoClient.write((uint8_t*)&len, 4);
        videoClient.write(fb->buf, fb->len);
        esp_camera_fb_return(fb);
        lastSend = millis();
      }
    }
  }
}
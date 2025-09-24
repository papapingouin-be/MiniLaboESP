/**
 * @file main.cpp
 * @brief Entrée principale de l'application MiniLabo.
 *
 * Ce firmware crée un serveur web asynchrone hébergé sur un ESP8266
 * permettant de configurer et de piloter un mini laboratoire
 * électronique.  Les configurations sont stockées dans des fichiers
 * JSON sur le système de fichiers flash (LittleFS) et sont chargées
 * et sauvegardées de manière atomique via la classe ConfigStore.  Un
 * système de journalisation léger enregistre les événements dans un
 * tampon circulaire et les persiste périodiquement sur la flash.
 *
 * Le serveur web expose des endpoints REST permettant de
 * consulter/modifier les fichiers de configuration et de récupérer
 * l'état des instruments (multimètre, oscilloscope, générateur de
 * fonctions).  La page d'accueil (data/index.html) fournit une
 * interface utilisateur sombre et futuriste conforme aux standards
 * industriels avec quatre cadrans pour le multimètre, l'oscilloscope,
 * le générateur et la liste des entrées/sorties.
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

#include "core/ConfigStore.h"
#include "core/Logger.h"
#include "core/IORegistry.h"

#include "ui/WebServer.h"
#include "OledPin.h"

#include "devices/DMM.h"
#include "devices/Scope.h"
#include "devices/FuncGen.h"
#include "network/UDPServer.h"

// Wi‑Fi mode fallback constants
static const char* DEFAULT_AP_SSID    = "MiniLabo";

struct WiFiStatusInfo {
  String line;
  String hardware;
  bool ready;
  bool apMode;
};

static WiFiStatusInfo g_wifiInfo = {String("WiFi: INIT"), String(), false, false};
static bool g_staConfigured = false;
static String g_staSSID;
static String g_staPass;
static String g_apSSID;
static String g_apPass;
static uint8_t g_apChannel = 6;
static bool g_apHidden = false;
static uint8_t g_apMaxClients = 4;
static IPAddress g_apIp(192, 168, 4, 1);
static IPAddress g_apGateway(192, 168, 4, 1);
static IPAddress g_apSubnet(255, 255, 255, 0);
static unsigned long g_lastReconnectAttempt = 0;
static const unsigned long STA_RETRY_INTERVAL_MS = 60000;  // 60 s de délai entre deux tentatives
static bool g_webAvailable = false;
static uint16_t g_webPort = 0;
static bool g_udpEnabled = false;
static bool g_udpRunning = false;
static uint16_t g_udpPort = 0;
static unsigned long g_lastStatusRefresh = 0;

static WiFiStatusInfo setupWiFi();
static void maintainWiFi();
static void updateStatusDisplay(bool force = false);
static void updateDisplayState();
static String computeWebStatus();
static String computeUdpStatus();
static String describeStaStatus(wl_status_t status);
static String computeApSSID(const String& configured);
static String byteToUpperHex(uint8_t value);
static bool startAccessPoint();
static String computeWifiHardware();
static String phyModeToString(WiFiPhyMode_t mode);
static String formatMacCompact(const uint8_t* mac);

/**
 * Configuration réseau initiale.
 *
 * Le mode est déterminé à partir de la configuration stockée dans
 * network.json.  Si le mode station (STA) échoue à se connecter,
 * l'appareil bascule automatiquement en point d'accès (AP) afin de
 * rester accessible pour la configuration initiale.
 */
static WiFiStatusInfo setupWiFi() {
  WiFiStatusInfo info = {String("WiFi: INIT"), String(), false, false};
  auto& net = ConfigStore::doc("network");
  String mode = net["mode"].as<String>();
  bool staRequested = (mode == "sta");
  if (net["sta"]["enabled"].is<bool>()) {
    staRequested = staRequested || net["sta"]["enabled"].as<bool>();
  }
  const char* staSsid = net["sta"]["ssid"].as<const char*>();
  const char* staPass = net["sta"]["password"].as<const char*>();
  if (!staSsid) staSsid = "";
  if (!staPass) staPass = "";
  g_staConfigured = staRequested && strlen(staSsid) > 0;
  g_staSSID = staSsid;
  g_staPass = staPass;

  String apConfigured = net["ap"]["ssid"].as<String>();
  String apPassword = net["ap"]["password"].as<String>();
  apPassword.trim();
  if (apPassword.length() == 0) {
    g_apPass = "";
  } else if (apPassword.length() < 8 || apPassword.length() > 63) {
    Logger::warn("NET", "setupWiFi",
                 String("Invalid AP password length (") + apPassword.length() +
                 ") - using open network");
    g_apPass = "";
  } else {
    g_apPass = apPassword;
  }
  int configuredChannel = net["ap"]["channel"].as<int>();
  if (configuredChannel < 1 || configuredChannel > 13) {
    configuredChannel = 6;
  }
  g_apChannel = static_cast<uint8_t>(configuredChannel);
  g_apHidden = net["ap"]["hidden"].as<bool>();
  int maxClients = net["ap"]["max_clients"].as<int>();
  if (maxClients < 1 || maxClients > 8) {
    maxClients = 4;
  }
  g_apMaxClients = static_cast<uint8_t>(maxClients);

  WiFi.persistent(false);
  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);

  if (g_staConfigured) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_staSSID.c_str(), g_staPass.c_str());
    g_lastReconnectAttempt = millis();
    Logger::info("NET", "setupWiFi", String("Connecting to ") + g_staSSID);
    unsigned long start = millis();
    // Tentative de connexion pendant 10 secondes
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
      delay(200);
      yield();
    }
    if (WiFi.status() == WL_CONNECTED) {
      g_apSSID = computeApSSID(apConfigured);
      Logger::info("NET", "setupWiFi", String("Connected, IP ") + WiFi.localIP().toString());
      info.line = String("WiFi: STA ") + WiFi.localIP().toString();
      info.hardware = computeWifiHardware();
      info.ready = true;

      // Conserve un point d'accès de secours même en mode STA afin
      // de garder une porte d'entrée pour la configuration.
      WiFi.mode(WIFI_AP_STA);
      bool apStarted = startAccessPoint();
      if (apStarted) {
        Logger::info("NET", "setupWiFi",
                     String("Fallback AP ready, SSID ") + g_apSSID +
                     " ch " + g_apChannel);
      } else {
        Logger::warn("NET", "setupWiFi", "Failed to start fallback AP");
        WiFi.mode(WIFI_STA);
      }
      info.apMode = apStarted;
      return info;
    }
    Logger::warn("NET", "setupWiFi", "STA connect failed, enabling AP");
  }

  WiFi.mode(g_staConfigured ? WIFI_AP_STA : WIFI_AP);
  g_apSSID = computeApSSID(apConfigured);
  if (startAccessPoint()) {
    Logger::info("NET", "setupWiFi",
                 String("AP started, SSID ") + g_apSSID + " ch " + g_apChannel);
    info.line = String("WiFi: AP ") + g_apSSID;
    info.hardware = computeWifiHardware();
    info.ready = false;
    info.apMode = true;
    if (g_staConfigured) {
      g_lastReconnectAttempt = millis();
    }
    return info;
  }
  Logger::error("NET", "setupWiFi", "Failed to start AP");
  info.line = String("WiFi: ERROR");
  info.hardware = String("AP init failed");
  info.ready = false;
  info.apMode = false;
  g_staConfigured = false;
  return info;
}

static String computeApSSID(const String& configured) {
  String base = configured;
  base.trim();
  if (base.length() == 0) {
    base = DEFAULT_AP_SSID;
  }

  uint8_t mac[6];
  WiFi.softAPmacAddress(mac);
  String suffix = byteToUpperHex(mac[4]) + byteToUpperHex(mac[5]);
  return base + suffix;
}

static String byteToUpperHex(uint8_t value) {
  String hex = String(value, HEX);
  hex.toUpperCase();
  if (hex.length() < 2) {
    hex = "0" + hex;
  }
  return hex;
}

static bool startAccessPoint() {
  // Redémarre proprement l'AP pour éviter les états incohérents.
  WiFi.softAPdisconnect(true);
  delay(100);

  const bool openAp = g_apPass.length() == 0;
  const char* passphrase = openAp ? "" : g_apPass.c_str();

  bool started = WiFi.softAP(g_apSSID.c_str(),
                             passphrase,
                             g_apChannel,
                             g_apHidden,
                             g_apMaxClients);
  if (!started) {
    delay(100);
    started = WiFi.softAP(g_apSSID.c_str(),
                          passphrase,
                          g_apChannel,
                          g_apHidden,
                          g_apMaxClients);
  }

  if (started) {
    if (!WiFi.softAPConfig(g_apIp, g_apGateway, g_apSubnet)) {
      Logger::warn("NET", "startAccessPoint", "softAPConfig failed, keeping default IP");
    }
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    delay(100);
  } else {
    Logger::error("NET", "startAccessPoint", "softAP start failed");
  }
  return started;
}

static String computeWifiHardware() {
  uint8_t mac[6];
  WiFi.softAPmacAddress(mac);
  String macStr = String("M") + formatMacCompact(mac);

  int channel = WiFi.channel();
  if (channel <= 0) {
    channel = g_apChannel;
  }

  //const char* phy = phyModeToString(WiFi.getPhyMode());
  String phy = phyModeToString(WiFi.getPhyMode());

  String hardware = macStr + String(" C") + channel + String("/") + phy;
  return hardware;
}

static String phyModeToString(WiFiPhyMode_t mode) {
  switch (mode) {
    case WIFI_PHY_MODE_11B: return "11B";
    case WIFI_PHY_MODE_11G: return "11G";
    case WIFI_PHY_MODE_11N: return "11N";
    default:                return "UNK";
  }
}

static String formatMacCompact(const uint8_t* mac) {
  char buf[13];
  snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

static String describeStaStatus(wl_status_t status) {
  switch (status) {
    case WL_NO_SSID_AVAIL:   return String("WiFi: STA no SSID");
    case WL_CONNECT_FAILED:  return String("WiFi: STA failed");
    case WL_WRONG_PASSWORD:  return String("WiFi: wrong pass");
    case WL_CONNECTION_LOST: return String("WiFi: STA lost");
    case WL_DISCONNECTED:    return String("WiFi: STA disc");
    case WL_IDLE_STATUS:     return String("WiFi: STA idle");
    default:                 return String("WiFi: STA ...");
  }
}

static String computeWebStatus() {
  if (g_webAvailable) {
    return String("Web: ON :") + g_webPort;
  }
  return String("Web: ERROR");
}

static String computeUdpStatus() {
  if (!g_udpEnabled) {
    return String("UDP: OFF");
  }
  if (g_udpRunning) {
    return String("UDP: ON :") + g_udpPort;
  }
  return String("UDP: ERROR");
}

static void updateStatusDisplay(bool force) {
  unsigned long now = millis();
  if (!force && now - g_lastStatusRefresh < 1000) {
    return;
  }
  g_lastStatusRefresh = now;

  String wifiLine = g_wifiInfo.line;
  if (g_wifiInfo.apMode) {
    wifiLine += String(" (") + WiFi.softAPgetStationNum() + ")";
  }
  String wifiHardware = g_wifiInfo.hardware;
  if (wifiHardware.length() == 0) {
    wifiHardware = computeWifiHardware();
  }
  String webLine = computeWebStatus();
  String udpLine = computeUdpStatus();

  static String prevWifi;
  static String prevWifiHardware;
  static String prevWeb;
  static String prevUdp;

  if (force || wifiLine != prevWifi || wifiHardware != prevWifiHardware || webLine != prevWeb || udpLine != prevUdp) {
    OledPin::showStatus(wifiLine, wifiHardware, webLine, udpLine);
    prevWifi = wifiLine;
    prevWifiHardware = wifiHardware;
    prevWeb = webLine;
    prevUdp = udpLine;
  }
}

static void maintainWiFi() {
  unsigned long now = millis();
  bool staConnected = WiFi.status() == WL_CONNECTED;
  bool apActive = WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA;
  bool apClients = apActive && WiFi.softAPgetStationNum() > 0;
  bool networkReady = staConnected || apClients;

  if (!staConnected && !apActive) {
    WiFi.mode(g_staConfigured ? WIFI_AP_STA : WIFI_AP);
    if (startAccessPoint()) {
      Logger::info("NET", "maintainWiFi",
                   String("AP restarted, SSID ") + g_apSSID + " ch " + g_apChannel);
      apActive = true;
    } else {
      Logger::error("NET", "maintainWiFi", "Failed to restart AP");
    }
  }

  String line;
  if (staConnected) {
    line = String("WiFi: STA ") + WiFi.localIP().toString();
  } else if (apClients) {
    line = String("WiFi: AP ") + g_apSSID;
  } else if (g_staConfigured) {
    line = describeStaStatus(WiFi.status());
  } else if (apActive) {
    line = String("WiFi: AP ") + g_apSSID;
  } else {
    line = String("WiFi: OFF");
  }

  String hardware = computeWifiHardware();

  bool changed = (line != g_wifiInfo.line) ||
                 (g_wifiInfo.hardware != hardware) ||
                 (g_wifiInfo.apMode != apActive) ||
                 (g_wifiInfo.ready != networkReady);
  g_wifiInfo.line = line;
  g_wifiInfo.hardware = hardware;
  g_wifiInfo.apMode = apActive;
  g_wifiInfo.ready = networkReady;

  if (changed) {
    updateStatusDisplay(true);
  } else {
    updateStatusDisplay(false);
  }

  if (g_staConfigured && !staConnected) {
    if (apClients) {
      // Un client est connecté sur l'AP : on diffère toute nouvelle tentative
      // pour éviter les coupures du SSID.
      g_lastReconnectAttempt = now;
    } else if (now - g_lastReconnectAttempt >= STA_RETRY_INTERVAL_MS) {
      Logger::info("NET", "maintainWiFi", "Retrying STA connection");
      WiFi.mode(WIFI_AP_STA);
      const char* passphrase = g_apPass.length() == 0 ? "" : g_apPass.c_str();
      WiFi.softAP(g_apSSID.c_str(), passphrase);
      WiFi.begin(g_staSSID.c_str(), g_staPass.c_str());
      g_lastReconnectAttempt = now;
    }
  }
}

static void updateDisplayState() {
  updateStatusDisplay(false);
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("[MiniLabo] Booting...");

  // Initialisation du système de fichiers
  if (!LittleFS.begin()) {
    Serial.println("[MiniLabo] Failed to mount LittleFS");
    return;
  }
  Serial.println("[MiniLabo] LittleFS mounted");

  // Chargement de toutes les configurations avant d'accéder au réseau
  ConfigStore::begin();

  // Setup WiFi (STA ou AP avec fallback)
  g_wifiInfo = setupWiFi();

  // Initialisation de l'afficheur OLED
  OledPin::begin();

  // Initialise le système de journalisation
  Logger::begin();

  // Enregistrement des entrées/sorties disponibles
  IORegistry::begin();

  // Démarre le serveur web et ses routes
  bool webStarted = WebServer::begin();
  g_webAvailable = webStarted && WebServer::isStarted();
  g_webPort = WebServer::port();

  // Démarre le serveur UDP pour diffusion/écoute
  g_udpRunning = UDPServer::begin();
  g_udpEnabled = UDPServer::isEnabled();
  g_udpPort = UDPServer::port();

  g_lastStatusRefresh = 0;

  updateStatusDisplay(true);

  Logger::info("SYS", "setup", "System initialised");
}

void loop() {
  // Mise à jour des modules récurrents
  ConfigStore::loop();
  Logger::loop();
  IORegistry::loop();
  // Mise à jour des instruments
  DMM::loop();
  Scope::loop();
  FuncGen::loop();
  WebServer::loop();
  UDPServer::loop();
  maintainWiFi();
  updateDisplayState();
  // Yield pour éviter le watchdog
  yield();
}
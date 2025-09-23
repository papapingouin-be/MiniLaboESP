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
static const char* DEFAULT_AP_PASS    = "12345678";

struct WiFiStatusInfo {
  String line;
  bool ready;
  bool apMode;
};

static WiFiStatusInfo g_wifiInfo = {String("WiFi: INIT"), false, false};
static bool g_staConfigured = false;
static String g_staSSID;
static String g_staPass;
static String g_apSSID;
static String g_apPass;
static unsigned long g_lastReconnectAttempt = 0;
static const unsigned long STA_RETRY_INTERVAL_MS = 60000;  // 60 s de délai entre deux tentatives
static bool g_webAvailable = false;
static uint16_t g_webPort = 0;
static bool g_udpEnabled = false;
static bool g_udpRunning = false;
static uint16_t g_udpPort = 0;
static int g_pinCode = 0;
static unsigned long g_lastStatusRefresh = 0;
static unsigned long g_lastIoRefresh = 0;
static bool g_pinShown = false;

enum class DisplayState { STATUS, PIN, IO };
static DisplayState g_displayState = DisplayState::STATUS;

static WiFiStatusInfo setupWiFi();
static void maintainWiFi();
static void updateStatusDisplay(bool force = false);
static void updateDisplayState();
static String computeWebStatus();
static String computeUdpStatus();
static String describeStaStatus(wl_status_t status);
static String computeApSSID(const String& configured);
static String byteToUpperHex(uint8_t value);

/**
 * Configuration réseau initiale.
 *
 * Le mode est déterminé à partir de la configuration stockée dans
 * network.json.  Si le mode station (STA) échoue à se connecter,
 * l'appareil bascule automatiquement en point d'accès (AP) afin de
 * rester accessible pour la configuration initiale.
 */
static WiFiStatusInfo setupWiFi() {
  WiFiStatusInfo info = {String("WiFi: INIT"), false, false};
  auto& net = ConfigStore::doc("network");
  String mode = net["mode"].as<String>();
  bool staRequested = (mode == "sta");
  if (net["sta"].containsKey("enabled")) {
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
  g_apPass = net["ap"]["password"].as<String>();
  if (g_apPass.length() < 8) g_apPass = DEFAULT_AP_PASS;

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
      info.ready = true;
      info.apMode = (WiFi.getMode() == WIFI_AP_STA);
      return info;
    }
    Logger::warn("NET", "setupWiFi", "STA connect failed, enabling AP");
  }

  WiFi.mode(g_staConfigured ? WIFI_AP_STA : WIFI_AP);
  g_apSSID = computeApSSID(apConfigured);
  if (WiFi.softAP(g_apSSID.c_str(), g_apPass.c_str())) {
    Logger::info("NET", "setupWiFi", String("AP started, SSID ") + g_apSSID);
    info.line = String("WiFi: AP ") + g_apSSID;
    info.ready = false;
    info.apMode = true;
    if (g_staConfigured) {
      g_lastReconnectAttempt = millis();
    }
    return info;
  }
  Logger::error("NET", "setupWiFi", "Failed to start AP");
  info.line = String("WiFi: ERROR");
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
  String webLine = computeWebStatus();
  String udpLine = computeUdpStatus();

  static String prevWifi;
  static String prevWeb;
  static String prevUdp;

  if (force || wifiLine != prevWifi || webLine != prevWeb || udpLine != prevUdp) {
    OledPin::showStatus(wifiLine, webLine, udpLine);
    prevWifi = wifiLine;
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
    if (WiFi.softAP(g_apSSID.c_str(), g_apPass.c_str())) {
      Logger::info("NET", "maintainWiFi", String("AP restarted, SSID ") + g_apSSID);
      apActive = true;
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

  bool changed = (line != g_wifiInfo.line) || (g_wifiInfo.apMode != apActive) || (g_wifiInfo.ready != networkReady);
  g_wifiInfo.line = line;
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
      WiFi.softAP(g_apSSID.c_str(), g_apPass.c_str());
      WiFi.begin(g_staSSID.c_str(), g_staPass.c_str());
      g_lastReconnectAttempt = now;
    }
  }
}

static void updateDisplayState() {
  if (!g_wifiInfo.ready) {
    if (g_displayState != DisplayState::STATUS) {
      g_displayState = DisplayState::STATUS;
      g_pinShown = false;
      updateStatusDisplay(true);
    }
    return;
  }

  if (g_displayState == DisplayState::STATUS) {
    g_displayState = DisplayState::PIN;
    if (!g_pinShown) {
      OledPin::showPIN(g_pinCode);
      g_pinShown = true;
    }
    return;
  }

  if (g_displayState == DisplayState::PIN) {
    if (WebServer::hasAuthenticatedClient()) {
      g_displayState = DisplayState::IO;
      g_lastIoRefresh = 0;
    }
    return;
  }

  if (g_displayState == DisplayState::IO) {
    unsigned long now = millis();
    if (now - g_lastIoRefresh >= 1000) {
      auto ios = IORegistry::list();
      OledPin::showIOValues(ios);
      g_lastIoRefresh = now;
    }
  }
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

  auto& general = ConfigStore::doc("general");
  g_pinCode = general["pin"].as<int>();

  g_displayState = DisplayState::STATUS;
  g_pinShown = false;
  g_lastStatusRefresh = 0;
  g_lastIoRefresh = 0;

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
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

/**
 * Configuration réseau initiale.
 *
 * Le mode est déterminé à partir de la configuration stockée dans
 * network.json.  Si le mode station (STA) échoue à se connecter,
 * l'appareil bascule automatiquement en point d'accès (AP) afin de
 * rester accessible pour la configuration initiale.
 */
static String setupWiFi() {
  auto& net = ConfigStore::doc("network");
  String mode = net["mode"].as<String>();
  if (mode == "sta") {
    const char* ssid = net["sta"]["ssid"].as<const char*>();
    const char* pass = net["sta"]["password"].as<const char*>();
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    Logger::info("NET", "setupWiFi", String("Connecting to ") + ssid);
    unsigned long start = millis();
    // Tentative de connexion pendant 10 secondes
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
      delay(500);
      yield();
    }
    if (WiFi.status() == WL_CONNECTED) {
      Logger::info("NET", "setupWiFi", String("Connected, IP ") + WiFi.localIP().toString());
      return String("WiFi: STA ") + WiFi.localIP().toString();
    }
    Logger::warn("NET", "setupWiFi", "STA connect failed, falling back to AP");
  }
  // Mode AP par défaut si la connexion STA échoue ou si mode != sta
  String ssid = net["ap"]["ssid"].as<String>();
  String pass = net["ap"]["password"].as<String>();
  if (ssid.length() == 0) ssid = DEFAULT_AP_SSID;
  if (pass.length() < 8) pass = DEFAULT_AP_PASS;
  WiFi.mode(WIFI_AP);
  if (WiFi.softAP(ssid.c_str(), pass.c_str())) {
    Logger::info("NET", "setupWiFi", String("AP started, SSID ") + ssid);
    return String("WiFi: AP ") + ssid;
  }
  Logger::error("NET", "setupWiFi", "Failed to start AP");
  return String("WiFi: ERROR");
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
  String wifiStatus = setupWiFi();

  // Initialisation de l'afficheur OLED
  OledPin::begin();

  // Initialise le système de journalisation
  Logger::begin();

  // Enregistrement des entrées/sorties disponibles
  IORegistry::begin();

  // Démarre le serveur web et ses routes
  bool webStarted = WebServer::begin();

  // Démarre le serveur UDP pour diffusion/écoute
  bool udpRunning = UDPServer::begin();

  String webStatus = webStarted && WebServer::isStarted()
      ? String("Web: ON :") + WebServer::port()
      : String("Web: ERROR");
  String udpStatus;
  if (!UDPServer::isEnabled()) {
    udpStatus = "UDP: OFF";
  } else if (udpRunning) {
    udpStatus = String("UDP: ON :") + UDPServer::port();
  } else {
    udpStatus = "UDP: ERROR";
  }

  auto& general = ConfigStore::doc("general");
  int pin = general["pin"].as<int>();
  OledPin::showStatus(wifiStatus, webStatus, udpStatus);
  delay(2000);
  OledPin::showPIN(pin);

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
  // Yield pour éviter le watchdog
  yield();
}
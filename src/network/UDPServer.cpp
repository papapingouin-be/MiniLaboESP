/**
 * @file UDPServer.cpp
 * @brief Implémentation du serveur UDP MiniLabo.
 */

#include "UDPServer.h"
#include "core/ConfigStore.h"
#include "core/Logger.h"
#include "devices/DMM.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>

namespace {
  static WiFiUDP _udp;
  static uint16_t _port = 50000;
  static uint16_t _destPort = 50000;
  static IPAddress _destAddr = IPAddress(255, 255, 255, 255);
  static bool _enabled = false;
  static bool _emitEnabled = false;
  static unsigned long _lastEmit = 0;
  static const unsigned long EMIT_INTERVAL_MS = 1000UL;

  /**
   * Lit la configuration réseau UDP dans network.json.  Les clés
   * suivantes sont optionnelles :
   * - udp_port (int) : port local d'écoute (par défaut 50000)
   * - udp_dest (string) : adresse IP de destination (par défaut
   *   "255.255.255.255" pour broadcast)
   * - udp_dest_port (int) : port de destination (par défaut 50000)
   * - udp_enabled (bool) : active le serveur UDP (par défaut false)
   * - udp_emit (bool) : active l'envoi périodique de données (par
   *   défaut false)
   */
  void loadConfig() {
    auto &net = ConfigStore::doc("network");
    if (net.containsKey("udp_port")) {
      _port = net["udp_port"].as<uint16_t>();
    }
    if (net.containsKey("udp_dest_port")) {
      _destPort = net["udp_dest_port"].as<uint16_t>();
    } else {
      _destPort = _port;
    }
    if (net.containsKey("udp_dest")) {
      const char *addr = net["udp_dest"].as<const char*>();
      _destAddr.fromString(addr);
    }
    if (net.containsKey("udp_enabled")) {
      _enabled = net["udp_enabled"].as<bool>();
    } else {
      _enabled = false;
    }
    if (net.containsKey("udp_emit")) {
      _emitEnabled = net["udp_emit"].as<bool>();
    } else {
      _emitEnabled = false;
    }
  }

  /**
   * Envoie un paquet JSON contenant les valeurs du multimètre sur le
   * port de destination.  Ce paquet a la structure suivante :
   * {
   *   "type": "dmm",
   *   "ts": <timestamp_ms>,
   *   "values": { "CH1": "1.234", ... }
   * }
   */
  void emitDMMValues() {
    DynamicJsonDocument doc(512);
    doc["type"] = "dmm";
    doc["ts"] = millis();
    JsonObject vals = doc["values"].to<JsonObject>();
    // Mise à jour des valeurs avant émission
    DMM::loop();
    DMM::values(vals);
    String json;
    serializeJson(doc, json);
    if (!_destAddr) return;
    _udp.beginPacket(_destAddr, _destPort);
    _udp.write((const uint8_t*)json.c_str(), json.length());
    _udp.endPacket();
  }
}

namespace UDPServer {

void begin() {
  // Charger la configuration
  loadConfig();
  if (!_enabled) {
    Logger::info("UDP", "begin", "UDP disabled in config");
    return;
  }
  if (!_udp.begin(_port)) {
    Logger::error("UDP", "begin", String("Failed to start UDP on port ") + _port);
    return;
  }
  Logger::info("UDP", "begin", String("UDP server listening on port ") + _port);
}

void loop() {
  if (!_enabled) return;
  // Réception des paquets
  int packetSize = _udp.parsePacket();
  if (packetSize > 0) {
    // Lecture du paquet
    String data;
    data.reserve(packetSize);
    while (_udp.available()) {
      char c = _udp.read();
      data += c;
    }
    // Tentative de parsing JSON
    DynamicJsonDocument doc(256);
    DeserializationError err = deserializeJson(doc, data);
    if (!err) {
      // Commande reconnue : on peut implémenter des actions
      const char* type = doc["type"].as<const char*>();
      if (type) {
        // Exemple : mise à jour du générateur de fonctions
        if (strcmp(type, "funcgen") == 0) {
          const char* target = doc["target"].as<const char*>();
          float freq = doc["freq"].as<float>();
          float amp = doc["amp"].as<float>();
          float off = doc["offset"].as<float>();
          const char* wave = doc["wave"].as<const char*>();
          FuncGen::updateTarget(String(target), freq, amp, off, String(wave));
        }
      }
    }
    // On pourrait renvoyer un ack ou une réponse selon la commande
  }
  // Émission périodique de valeurs
  if (_emitEnabled) {
    unsigned long now = millis();
    if (now - _lastEmit >= EMIT_INTERVAL_MS) {
      emitDMMValues();
      _lastEmit = now;
    }
  }
}

void setEmitEnabled(bool enable) {
  _emitEnabled = enable;
}

} // namespace UDPServer
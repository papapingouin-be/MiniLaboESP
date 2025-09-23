/**
 * @file UDPServer.cpp
 * @brief ImplÃ©mentation du serveur UDP MiniLabo.
 */

#include "UDPServer.h"
#include "core/ConfigStore.h"
#include "core/Logger.h"
#include "devices/DMM.h"
#include "devices/FuncGen.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>

namespace {
  static WiFiUDP _udp;
  static uint16_t _port = 50000;
  static uint16_t _destPort = 50000;
  static IPAddress _destAddr = IPAddress(255, 255, 255, 255);
  static bool _enabled = false;
  static bool _running = false;
  static bool _emitEnabled = false;
  static unsigned long _lastEmit = 0;
  static const unsigned long EMIT_INTERVAL_MS = 1000UL;

  /**
   * Lit la configuration rÃ©seau UDP dans network.json.  Les clÃ©s
   * suivantes sont optionnelles :
   * - udp_port (int) : port local d'Ã©coute (par dÃ©faut 50000)
   * - udp_dest (string) : adresse IP de destination (par dÃ©faut
   *   "255.255.255.255" pour broadcast)
   * - udp_dest_port (int) : port de destination (par dÃ©faut 50000)
   * - udp_enabled (bool) : active le serveur UDP (par dÃ©faut false)
   * - udp_emit (bool) : active l'envoi pÃ©riodique de donnÃ©es (par
   *   dÃ©faut false)
   */
  void loadConfig() {
    auto &net = ConfigStore::doc("network");
    if (!net["udp_port"].isNull()) {
      _port = net["udp_port"].as<uint16_t>();
    }
    if (!net["udp_dest_port"].isNull()) {
      _destPort = net["udp_dest_port"].as<uint16_t>();
    } else {
      _destPort = _port;
    }
    if (!net["udp_dest"].isNull()) {
      const char *addr = net["udp_dest"].as<const char*>();
      _destAddr.fromString(addr);
    }
    if (!net["udp_enabled"].isNull()) {
      _enabled = net["udp_enabled"].as<bool>();
    } else {
      _enabled = false;
    }
    if (!net["udp_emit"].isNull()) {
      _emitEnabled = net["udp_emit"].as<bool>();
    } else {
      _emitEnabled = false;
    }
  }

  /**
   * Envoie un paquet JSON contenant les valeurs du multimÃ¨tre sur le
   * port de destination.  Ce paquet a la structure suivanteÂ :
   * {
   *   "type": "dmm",
   *   "ts": <timestamp_ms>,
   *   "values": { "CH1": "1.234", ... }
   * }
   */
  void emitDMMValues() {
    JsonDocument doc;
    doc["type"] = "dmm";
    doc["ts"] = millis();
    JsonObject vals = doc["values"].to<JsonObject>();
    // Mise Ã  jour des valeurs avant Ã©mission
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

bool begin() {
  // Charger la configuration
  loadConfig();
  _running = false;
  if (!_enabled) {
    Logger::info("UDP", "begin", "UDP disabled in config");
    return false;
  }
  if (!_udp.begin(_port)) {
    Logger::error("UDP", "begin", String("Failed to start UDP on port ") + _port);
    return false;
  }
  Logger::info("UDP", "begin", String("UDP server listening on port ") + _port);
  _running = true;
  return true;
}

void loop() {
  if (!_enabled) return;
  // RÃ©ception des paquets
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
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data);
    if (!err) {
      // Commande reconnue : on peut implÃ©menter des actions
      const char* type = doc["type"].as<const char*>();
      if (type) {
        // Exemple : mise Ã  jour du gÃ©nÃ©rateur de fonctions
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
    // On pourrait renvoyer un ack ou une rÃ©ponse selon la commande
  }
  // Ã‰mission pÃ©riodique de valeurs
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

bool isEnabled() {
  return _enabled;
}

uint16_t port() {
  return _port;
}

} // namespace UDPServer

/**
 * @file DMM.cpp
 * @brief Implémentation du multimètre virtuel.
 */

#include "DMM.h"

#include <ArduinoJson.h>
#include "core/ConfigStore.h"
#include "core/Logger.h"

std::vector<DMM::Channel> DMM::_channels;

void DMM::begin() {
  _channels.clear();
  // Charger la config des canaux
  auto& doc = ConfigStore::doc("dmm");
  JsonArray channels = doc["channels"].as<JsonArray>();
  for (JsonObject ch : channels) {
    String name = ch["name"].as<String>();
    String source = ch["source"].as<String>();
    String mode = ch["mode"].as<String>();
    uint8_t decimals = ch["decimals"].as<uint8_t>();
    size_t window = ch["filter_window"].as<size_t>();
    IOBase* io = IORegistry::get(source);
    if (!io) {
      Logger::warn("DMM", "begin", String("Unknown IO for channel ") + name + ": " + source);
      continue;
    }
    Channel c;
    c.name = name;
    c.io = io;
    c.mode = mode;
    c.decimals = decimals;
    c.window = window < 1 ? 1 : window;
    c.buffer.reserve(c.window);
    c.sum = 0.0f;
    c.last = 0.0f;
    _channels.push_back(c);
    Logger::info("DMM", "begin", String("Channel ") + name + " -> " + source);
  }
}

void DMM::loop() {
  for (auto &ch : _channels) {
    float raw = ch.io->readRaw();
    // Conversion simple en tension DC (vref * ratio)
    float value = raw * ch.io->getVref() * ch.io->getRatio();
    // Mise à jour du filtre moyenne glissante
    if (ch.buffer.size() < ch.window) {
      ch.buffer.push_back(value);
      ch.sum += value;
    } else {
      // retirer la plus ancienne
      ch.sum -= ch.buffer[0];
      // décaler les éléments
      ch.buffer.erase(ch.buffer.begin());
      ch.buffer.push_back(value);
      ch.sum += value;
    }
    ch.last = ch.sum / ch.buffer.size();
  }
}

void DMM::values(JsonObject& out) {
  for (auto &ch : _channels) {
    // Formatage avec nombre de décimales configuré
    char buf[32];
    dtostrf(ch.last, 0, ch.decimals, buf);
    out[ch.name] = String(buf);
  }
}
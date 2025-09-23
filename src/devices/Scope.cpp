/**
 * @file Scope.cpp
 * @brief Implémentation minimale de l'oscilloscope virtuel.
 */

#include "Scope.h"
#include "core/ConfigStore.h"
#include "core/Logger.h"

#include <ArduinoJson.h>

std::vector<Scope::Channel> Scope::_channels;

void Scope::begin() {
  _channels.clear();
  auto& doc = ConfigStore::doc("scope");
  JsonArray channels = doc["channels"].as<JsonArray>();
  for (JsonObject ch : channels) {
    String name = ch["name"].as<String>();
    String source = ch["source"].as<String>();
    float amp = ch["amplitude"].as<float>();
    float offset = ch["offset"].as<float>();
    size_t size = ch["buffer_size"].as<size_t>();
    IOBase* io = IORegistry::get(source);
    if (!io) {
      Logger::warn("SCOPE", "begin", String("Unknown IO for scope: ") + source);
      continue;
    }
    Channel c;
    c.name = name;
    c.io = io;
    c.amplitude = amp;
    c.offset = offset;
    c.bufferSize = size > 0 ? size : 256;
    c.buffer.reserve(c.bufferSize);
    _channels.push_back(c);
  }
}

void Scope::loop() {
  // Échantillonne chaque canal et stocke la valeur dans un tampon
  // circulaire.  Les valeurs sont converties en tension physique
  // via getVref() et getRatio(), puis mises à l'échelle selon
  // l'amplitude et l'offset configurés pour ce canal.  Le tampon
  // conserve un nombre fixe d'échantillons défini par bufferSize.
  for (auto &ch : _channels) {
    float raw = ch.io->readRaw();
    float value = raw * ch.io->getVref() * ch.io->getRatio();
    // Mise à l'échelle : valeur relative à l'offset puis divisée par
    // l'amplitude.  Si amplitude vaut 0, on évite la division.
    float scaled;
    if (ch.amplitude > 0.0f) {
      scaled = (value - ch.offset) / ch.amplitude;
    } else {
      scaled = value - ch.offset;
    }
    // Ajoute au tampon
    if (ch.buffer.size() < ch.bufferSize) {
      ch.buffer.push_back(scaled);
    } else {
      // déplacement circulaire : on supprime le plus ancien
      ch.buffer.erase(ch.buffer.begin());
      ch.buffer.push_back(scaled);
    }
  }
}

void Scope::toJson(JsonObject& out) {
  // Retourne l'état de chaque canal sous forme de tableau JSON.  On
  // transmet les valeurs brutes mises à l'échelle (scaled).  Cette
  // méthode peut être appelée depuis l'API REST pour récupérer un
  // ensemble d'échantillons pour l'affichage côté client.
  for (auto &ch : _channels) {
    JsonArray buf = out[ch.name].to<JsonArray>();
    for (float v : ch.buffer) {
      buf.add(v);
    }
  }
}
/**
 * @file ConfigStore.cpp
 * @brief Implémentation de la gestion centralisée des fichiers de
 * configuration JSON.
 */

#include "ConfigStore.h"
#include "Logger.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

// Définition des constantes statiques
std::map<String, ConfigStore::AreaState> ConfigStore::_areas;
unsigned long ConfigStore::_lastSave = 0;
const unsigned long ConfigStore::DEBOUNCE_MS   = 1000UL;
const unsigned long ConfigStore::MIN_PERIOD_MS = 2000UL;

void ConfigStore::begin() {
  // Initialise la liste des zones et leurs fichiers associés
  const struct { const char *area; const char *file; size_t capacity; } areas[] = {
    {"general",  "/configuration/general.json",  1024},
    {"network",  "/configuration/network.json",  1024},
    {"io",       "/configuration/io.json",       2048},
    {"dmm",      "/configuration/dmm.json",      2048},
    {"scope",    "/configuration/scope.json",    2048},
    {"funcgen",  "/configuration/funcgen.json",  1024},
    {"math",     "/configuration/math.json",     2048}
  };

  for (const auto &def : areas) {
    DynamicJsonDocument *doc = new DynamicJsonDocument(def.capacity);
    AreaState state;
    state.filename = def.file;
    state.document = doc;
    state.dirty = false;
    state.lastChange = 0;
    _areas[String(def.area)] = state;
    loadArea(def.area);
  }
}

DynamicJsonDocument& ConfigStore::doc(const String& area) {
  return *(_areas[area].document);
}

void ConfigStore::requestSave(const String& area) {
  auto it = _areas.find(area);
  if (it != _areas.end()) {
    it->second.dirty = true;
    it->second.lastChange = millis();
  }
}

void ConfigStore::loop() {
  unsigned long now = millis();
  // Vérifie toutes les zones pour lesquelles une sauvegarde est demandée
  for (auto &kv : _areas) {
    AreaState &s = kv.second;
    if (s.dirty) {
      if (now - s.lastChange >= DEBOUNCE_MS && now - _lastSave >= MIN_PERIOD_MS) {
        if (saveArea(kv.first)) {
          s.dirty = false;
          _lastSave = now;
        }
      }
    }
  }
}

void ConfigStore::loadArea(const String& area) {
  auto it = _areas.find(area);
  if (it == _areas.end()) return;
  AreaState &s = it->second;
  File f = LittleFS.open(s.filename, "r");
  if (!f) {
    Logger::warn("CFG", "loadArea", String("File not found, using defaults: ") + s.filename);
    // Définition des valeurs par défaut minimalistes
    // Général
    if (area == "general") {
      s.document->clear();
      (*s.document)["pin"] = 1234;
      (*s.document)["version"] = 1;
      JsonArray ui = (*s.document)["ui"].to<JsonArray>();
      ui.add("dmm"); ui.add("scope"); ui.add("funcgen"); ui.add("io");
    } else if (area == "network") {
      s.document->clear();
      (*s.document)["mode"] = "ap";
      JsonObject ap = (*s.document)["ap"].to<JsonObject>();
      ap["ssid"] = "MiniLabo";
      ap["password"] = "12345678";
      JsonObject sta = (*s.document)["sta"].to<JsonObject>();
      sta["enabled"] = false;
      sta["ssid"] = "";
      sta["password"] = "";
    } else if (area == "io") {
      s.document->clear();
      JsonArray devices = (*s.document)["devices"].to<JsonArray>();
      JsonObject d0 = devices.add<JsonObject>();
      d0["id"] = "IO_A0";
      d0["type"] = "adc";
      d0["driver"] = "a0";
      d0["bits"] = 10;
      d0["vref"] = 1.0;
      d0["ratio"] = 3.3;
    } else if (area == "dmm") {
      s.document->clear();
      JsonArray channels = (*s.document)["channels"].to<JsonArray>();
      JsonObject c0 = channels.add<JsonObject>();
      c0["name"] = "CH1";
      c0["source"] = "IO_A0";
      c0["mode"] = "UDC";
      c0["decimals"] = 3;
      c0["filter_window"] = 16;
    } else {
      s.document->clear();
    }
    return;
  }
  // Désérialisation du JSON
  DeserializationError err = deserializeJson(*s.document, f);
  if (err) {
    Logger::error("CFG", "loadArea", String("Failed to parse ") + s.filename + ": " + err.c_str());
    // Vider document en cas d'erreur
    s.document->clear();
  }
  f.close();
}

bool ConfigStore::saveArea(const String& area) {
  auto it = _areas.find(area);
  if (it == _areas.end()) return false;
  AreaState &s = it->second;
  String tmpName = s.filename + String(".tmp");
  {
    // Écrire dans un fichier temporaire
    File f = LittleFS.open(tmpName, "w");
    if (!f) {
      Logger::error("CFG", "saveArea", String("Failed to open tmp for ") + tmpName);
      return false;
    }
    if (serializeJson(*s.document, f) == 0) {
      Logger::error("CFG", "saveArea", String("serializeJson failed for ") + area);
      f.close();
      return false;
    }
    f.flush();
    f.close();
  }
  // Renommer atomiquement l'ancien fichier
  LittleFS.remove(s.filename);
  if (!LittleFS.rename(tmpName, s.filename)) {
    Logger::error("CFG", "saveArea", String("rename failed for ") + s.filename);
    return false;
  }
  Logger::info("CFG", "saveArea", String("Saved ") + s.filename);
  return true;
}
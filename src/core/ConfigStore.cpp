/**
 * @file ConfigStore.cpp
 * @brief ImplÃ©mentation de la gestion centralisÃ©e des fichiers de
 * configuration JSON.
 */

#include "ConfigStore.h"
#include "Logger.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

namespace {
  struct AreaDefinition {
    const char *area;
    const char *file;
    size_t capacity;
  };

  void applyDefaults(const String& area, JsonDocument& doc) {
    doc.clear();
    if (area == "general") {
      doc["pin"] = 1234;
      doc["version"] = 1;
      JsonArray ui = doc["ui"].to<JsonArray>();
      ui.add("dmm");
      ui.add("scope");
      ui.add("funcgen");
      ui.add("io");
    } else if (area == "network") {
      JsonObject net = doc.to<JsonObject>();
      net["mode"] = "ap";
      JsonObject ap = net["ap"].to<JsonObject>();
      ap["ssid"] = "MiniLabo";
      ap["password"] = "12345678";
      JsonObject sta = net["sta"].to<JsonObject>();
      sta["enabled"] = false;
      sta["ssid"] = "";
      sta["password"] = "";
      net["udp_enabled"] = false;
      net["udp_port"] = 50000;
      net["udp_dest"] = "255.255.255.255";
      net["udp_dest_port"] = 50000;
      net["udp_emit"] = false;
    } else if (area == "io") {
      JsonArray devices = doc["devices"].to<JsonArray>();
      JsonObject d0 = devices.add<JsonObject>();
      d0["id"] = "IO_A0";
      d0["type"] = "adc";
      d0["driver"] = "a0";
      d0["bits"] = 10;
      d0["vref"] = 1.0;
      d0["ratio"] = 3.3;
    } else if (area == "dmm") {
      JsonArray channels = doc["channels"].to<JsonArray>();
      JsonObject c0 = channels.add<JsonObject>();
      c0["name"] = "CH1";
      c0["source"] = "IO_A0";
      c0["mode"] = "UDC";
      c0["decimals"] = 3;
      c0["filter_window"] = 16;
    } else {
      doc.clear();
    }
  }
}

// DÃ©finition des constantes statiques
std::map<String, ConfigStore::AreaState> ConfigStore::_areas;
unsigned long ConfigStore::_lastSave = 0;
const unsigned long ConfigStore::DEBOUNCE_MS   = 1000UL;
const unsigned long ConfigStore::MIN_PERIOD_MS = 2000UL;

void ConfigStore::begin() {
  if (!LittleFS.exists("/configuration")) {
    if (!LittleFS.mkdir("/configuration")) {
      Logger::error("CFG", "begin", "Failed to create /configuration directory");
    }
  }

  // Initialise la liste des zones et leurs fichiers associÃ©s
  const AreaDefinition areas[] = {
    {"general",  "/configuration/general.json", 1024},
    {"network",  "/configuration/network.json", 1024},
    {"io",       "/configuration/io.json",      2048},
    {"dmm",      "/configuration/dmm.json",     1024},
    {"scope",    "/configuration/scope.json",   2048},
    {"funcgen",  "/configuration/funcgen.json", 1024},
    {"math",     "/configuration/math.json",    1024}
  };

  for (const auto &def : areas) {
    JsonDocument* doc = new DynamicJsonDocument(def.capacity);
    AreaState state;
    state.filename = def.file;
    state.document = doc;
    state.dirty = false;
    state.lastChange = 0;
    _areas[String(def.area)] = state;
    loadArea(def.area);
  }
}

JsonDocument& ConfigStore::doc(const String& area) {
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
  // VÃ©rifie toutes les zones pour lesquelles une sauvegarde est demandÃ©e
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
    applyDefaults(area, *s.document);
    saveArea(area);
    return;
  }
  // DÃ©sÃ©rialisation du JSON
  DeserializationError err = deserializeJson(*s.document, f);
  if (err) {
    Logger::error("CFG", "loadArea", String("Failed to parse ") + s.filename + ": " + err.c_str());
    applyDefaults(area, *s.document);
    saveArea(area);
  }
  f.close();
}

bool ConfigStore::saveArea(const String& area) {
  auto it = _areas.find(area);
  if (it == _areas.end()) return false;
  AreaState &s = it->second;
  String tmpName = s.filename + String(".tmp");
  {
    // Ã‰crire dans un fichier temporaire
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

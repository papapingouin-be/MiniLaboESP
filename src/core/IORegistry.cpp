/**
 * @file IORegistry.cpp
 * @brief Implémentation du registre d'IO logiques.
 */

#include "IORegistry.h"
#include "ConfigStore.h"
#include "Logger.h"

#include <ArduinoJson.h>

// Ajout des pilotes externes pour les ADC et DAC I2C
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_MCP4725.h>

// Carte NodeMCU ESP8266: broche PWM utilisée pour le module 0–10 V
// Le choix de la broche dépend du matériel; D1 (GPIO5) est un exemple.
// Broche PWM pour la sortie 0–10 V.  Choisissez une broche libre qui
// supporte analogWrite (par ex. D3 sur NodeMCU).  Ce paramètre peut
// être ajusté selon votre schéma matériel.
static constexpr uint8_t PIN_0_10V_OUT = D3;

// Cartes partagées pour gérer plusieurs périphériques sur le même bus
static std::map<uint8_t, Adafruit_ADS1115*> _adsDevices;
static std::map<uint8_t, Adafruit_MCP4725*> _dacDevices;

/**
 * Convertit une tension plein‑échelle (pga en volts) en constante
 * adsGain_t pour la bibliothèque Adafruit ADS1X15.  La carte ADS1115
 * propose plusieurs gains correspondants aux valeurs ±6.144 V,
 * ±4.096 V, ±2.048 V, ±1.024 V, ±0.512 V et ±0.256 V.  On choisit
 * le gain le plus élevé inférieur ou égal à la valeur passée.
 */
static adsGain_t pgaToGain(float pga) {
  if (pga >= 6.144f) return GAIN_TWOTHIRDS; // ±6.144 V
  if (pga >= 4.096f) return GAIN_ONE;       // ±4.096 V
  if (pga >= 2.048f) return GAIN_TWO;       // ±2.048 V
  if (pga >= 1.024f) return GAIN_FOUR;      // ±1.024 V
  if (pga >= 0.512f) return GAIN_EIGHT;     // ±0.512 V
  return GAIN_SIXTEEN;                      // ±0.256 V
}

std::vector<IOBase*> IORegistry::_list;
std::map<String, IOBase*> IORegistry::_map;

void IORegistry::registerIO(IOBase* io) {
  _list.push_back(io);
  _map[io->id()] = io;
  Logger::info("IO", "registerIO", String("Registered ") + io->id());
}

void IORegistry::begin() {
  // Libère les IO existantes si begin() est appelé à nouveau
  for (auto io : _list) {
    delete io;
  }
  _list.clear();
  _map.clear();
  // Charge la configuration et crée les IO
  auto& doc = ConfigStore::doc("io");
  JsonArray devices = doc["devices"].as<JsonArray>();
  for (JsonObject dev : devices) {
    String id    = dev["id"].as<String>();
    String drv   = dev["driver"].as<String>();
    if (drv == "a0") {
      int bits = dev["bits"].as<int>();
      float vref = dev["vref"].as<float>();
      float ratio = dev["ratio"].as<float>();
      registerIO(new IO_A0(id, bits, vref, ratio));
    } else if (drv == "ads1115") {
      uint8_t addr = dev["i2c_addr"].as<uint8_t>();
      uint8_t channel = dev["channel"].as<uint8_t>();
      float pga = dev["pga"].as<float>();
      registerIO(new IO_ADS1115(id, addr, channel, pga));
    } else if (drv == "mcp4725") {
      uint8_t addr = dev["i2c_addr"].as<uint8_t>();
      int bits = dev["bits"].as<int>();
      float vref = dev["vref"].as<float>();
      registerIO(new IO_MCP4725(id, addr, bits, vref));
    } else if (drv == "0_10v") {
      registerIO(new IO_0_10V(id));
    } else {
      Logger::warn("IO", "begin", String("Unknown driver: ") + drv);
    }
  }
}

IOBase* IORegistry::get(const String &id) {
  auto it = _map.find(id);
  if (it != _map.end()) return it->second;
  return nullptr;
}

std::vector<IOBase*> IORegistry::list() {
  return _list;
}

/*
 * Implementations spécifiques des IO dérivées.  Ces méthodes
 * utilisent les bibliothèques Adafruit pour accéder aux
 * périphériques I2C.  Les appareils sont instanciés et mis en
 * cache dans des maps statiques afin de partager un pilote par
 * adresse I2C.
 */

float IO_ADS1115::readRaw() {
  // Obtenir ou créer l'instance du convertisseur pour cette adresse
  Adafruit_ADS1115 *ads;
  auto it = _adsDevices.find(_address);
  if (it == _adsDevices.end()) {
    ads = new Adafruit_ADS1115();
    if (!ads->begin(_address)) {
      Logger::error("IO", "ADS1115", String("Failed to init ADS1115 at 0x") + String(_address, HEX));
      // Conserver l'instance malgré tout pour éviter les nullptr
    }
    _adsDevices[_address] = ads;
  } else {
    ads = it->second;
  }
  // Configurer le gain en fonction de la pleine échelle souhaitée
  ads->setGain(pgaToGain(_pga));
  // Lecture en mode single-ended : valeur 0..32767
  int16_t code = ads->readADC_SingleEnded(_channel);
  // Normaliser en fraction 0..1 (code max 32767)
  float frac = static_cast<float>(code) / 32767.0f;
  if (frac < 0.0f) frac = 0.0f;
  if (frac > 1.0f) frac = 1.0f;
  return frac;
}

void IO_MCP4725::writePercent(float percent) {
  // Clamp du pourcentage 0–100
  if (percent < 0.0f) percent = 0.0f;
  if (percent > 100.0f) percent = 100.0f;
  float ratio = percent / 100.0f;
  // Obtenir ou créer l'instance du DAC
  Adafruit_MCP4725 *dac;
  auto it = _dacDevices.find(_address);
  if (it == _dacDevices.end()) {
    dac = new Adafruit_MCP4725();
    if (!dac->begin(_address)) {
      Logger::error("IO", "MCP4725", String("Failed to init MCP4725 at 0x") + String(_address, HEX));
    }
    _dacDevices[_address] = dac;
  } else {
    dac = it->second;
  }
  // Convertir en code 0..2^bits-1
  uint16_t maxCode = (1u << _bits) - 1;
  uint16_t code = static_cast<uint16_t>(ratio * maxCode + 0.5f);
  dac->setVoltage(code, false);
}

void IO_0_10V::writePercent(float percent) {
  // Clamp du pourcentage 0–100
  if (percent < 0.0f) percent = 0.0f;
  if (percent > 100.0f) percent = 100.0f;
  float ratio = percent / 100.0f;
  // Conversion en valeur PWM 0..1023 (10 bits) pour ESP8266
  uint16_t pwm = static_cast<uint16_t>(ratio * 1023.0f + 0.5f);
  analogWrite(PIN_0_10V_OUT, pwm);
}

/**
 * @file FuncGen.cpp
 * @brief Implémentation du générateur de fonctions.
 */

#include "FuncGen.h"
#include "core/ConfigStore.h"
#include "core/Logger.h"

IOBase* FuncGen::_target = nullptr;
float FuncGen::_freq = 50.0f;
float FuncGen::_amp = 0.5f;    // amplitude 0..1
float FuncGen::_offset = 0.0f; // offset 0..1
String FuncGen::_wave = "sine";
unsigned long FuncGen::_start = 0;

void FuncGen::begin() {
  auto& doc = ConfigStore::doc("funcgen");
  String targetId = doc["target"].as<String>();
  _freq = doc["freq"].as<float>();
  _amp = doc["amp"].as<float>() / 100.0f;
  _offset = doc["offset"].as<float>() / 100.0f;
  _wave = doc["wave"].as<String>();
  _start = millis();
  _target = IORegistry::get(targetId);
  if (!_target) {
    Logger::warn("FUNC", "begin", String("Unknown target IO: ") + targetId);
  }
}

void FuncGen::updateTarget(const String& id, float freq, float amp, float off, const String& wave) {
  _target = IORegistry::get(id);
  if (!_target) {
    Logger::warn("FUNC", "updateTarget", String("Unknown target: ") + id);
  }
  _freq = freq;
  _amp = amp / 100.0f;
  _offset = off / 100.0f;
  _wave = wave;
  _start = millis();
  // Met à jour également la configuration persistante
  auto& doc = ConfigStore::doc("funcgen");
  doc["target"] = id;
  doc["freq"] = freq;
  doc["amp"] = amp;
  doc["offset"] = off;
  doc["wave"] = wave;
  ConfigStore::requestSave("funcgen");
}

void FuncGen::loop() {
  if (!_target) return;
  float t = (millis() - _start) / 1000.0f;
  float x = 0.0f;
  if (_wave == "sine") {
    x = sinf(2.0f * PI * _freq * t);
  } else if (_wave == "square") {
    x = sinf(2.0f * PI * _freq * t) >= 0.0f ? 1.0f : -1.0f;
  } else if (_wave == "triangle") {
    float phase = fmodf(t * _freq, 1.0f);
    x = phase < 0.5f ? (phase * 4.0f - 1.0f) : (3.0f - phase * 4.0f);
  } else {
    x = 0.0f;
  }
  float y = _offset + (_amp / 2.0f) * x + _amp / 2.0f;
  // Clamp 0–1
  if (y < 0.0f) y = 0.0f;
  if (y > 1.0f) y = 1.0f;
  // Convert to percent for writePercent()
  _target->writePercent(y * 100.0f);
}
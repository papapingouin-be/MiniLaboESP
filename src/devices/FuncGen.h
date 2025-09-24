/**
 * @file FuncGen.h
 * @brief Générateur de fonctions pour MiniLabo (squelette).
 *
 * Cette classe pilote une sortie analogique (DAC ou module 0–10 V)
 * selon des paramètres définis dans la configuration.  Les formes
 * d'onde supportées sont sinusoïde, carré et triangle.  L'appel
 * périodique à tick() génère une nouvelle consigne en fonction du
 * temps écoulé.
 */

#pragma once

#include <Arduino.h>
#include "core/IORegistry.h"

class FuncGen {
public:
  static void begin();
  static void loop();
  /** Met à jour la configuration via l'API REST (appel depuis WebServer). */
  static void updateTarget(const String& id, float freq, float amp, float off, const String& wave);
private:
  static IOBase* _target;
  static float _freq;
  static float _amp;
  static float _offset;
  static String _wave;
  static unsigned long _start;
};
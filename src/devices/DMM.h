/**
 * @file DMM.h
 * @brief Multimètre virtuel gérant plusieurs canaux de mesure.
 *
 * Le multimètre lit des valeurs brutes sur des IO référencées et
 * applique une conversion simple en tension DC.  Les autres modes
 * (RMS, fréquence, courant) sont à implémenter ultérieurement.  Les
 * valeurs lissées sont retournées sous forme de chaîne formatée.
 */

#pragma once

#include <Arduino.h>
#include <vector>
#include "core/IORegistry.h"

class DMM {
public:
  /** Initialise les canaux du multimètre à partir de la config. */
  static void begin();
  /** Effectue l'acquisition et le filtrage des mesures. */
  static void loop();
  /** Retourne l'ensemble des valeurs formatées pour l'API REST. */
  static void values(JsonObject& out);
private:
  struct Channel {
    String name;
    IOBase* io;
    String mode;
    uint8_t decimals;
    size_t window;
    std::vector<float> buffer;
    float sum;
    float last;
  };
  static std::vector<Channel> _channels;
};
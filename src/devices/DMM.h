/**
 * @file DMM.h
 * @brief MultimÃ¨tre virtuel gÃ©rant plusieurs canaux de mesure.
 *
 * Le multimÃ¨tre lit des valeurs brutes sur des IO rÃ©fÃ©rencÃ©es et
 * applique une conversion simple en tension DC.  Les autres modes
 * (RMS, frÃ©quence, courant) sont Ã  implÃ©menter ultÃ©rieurement.  Les
 * valeurs lissÃ©es sont retournÃ©es sous forme de chaÃ®ne formatÃ©e.
 */

#pragma once

#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>
#include "core/IORegistry.h"

class DMM {
public:
  /** Initialise les canaux du multimÃ¨tre Ã  partir de la config. */
  static void begin();
  /** Effectue l'acquisition et le filtrage des mesures. */
  static void loop();
  /** Retourne l'ensemble des valeurs formatÃ©es pour l'API REST. */
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

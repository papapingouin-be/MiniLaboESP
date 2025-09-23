/**
 * @file Scope.h
 * @brief Oscilloscope virtuel (squelette).
 *
 * L'oscilloscope affiche des Ã©chantillons provenant de plusieurs
 * canaux.  Cette implÃ©mentation est volontairement minimale : elle
 * prÃ©pare la structure de donnÃ©es mais ne rÃ©alise pas encore le
 * stockage temps rÃ©el ni l'affichage graphique.  Vous pouvez
 * Ã©tendre la mÃ©thode loop() pour enregistrer les Ã©chantillons dans
 * un tampon circulaire par canal et calculer la mise Ã  l'Ã©chelle
 * selon l'amplitude et l'offset.
 */

#pragma once

#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>
#include "core/IORegistry.h"

class Scope {
public:
  static void begin();
  static void loop();
  static void toJson(JsonObject& out);
private:
  struct Channel {
    String name;
    IOBase* io;
    float amplitude;
    float offset;
    size_t bufferSize;
    std::vector<float> buffer;
  };
  static std::vector<Channel> _channels;
};

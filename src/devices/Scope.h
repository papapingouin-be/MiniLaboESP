/**
 * @file Scope.h
 * @brief Oscilloscope virtuel (squelette).
 *
 * L'oscilloscope affiche des échantillons provenant de plusieurs
 * canaux.  Cette implémentation est volontairement minimale : elle
 * prépare la structure de données mais ne réalise pas encore le
 * stockage temps réel ni l'affichage graphique.  Vous pouvez
 * étendre la méthode loop() pour enregistrer les échantillons dans
 * un tampon circulaire par canal et calculer la mise à l'échelle
 * selon l'amplitude et l'offset.
 */

#pragma once

#include <Arduino.h>
#include <vector>
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
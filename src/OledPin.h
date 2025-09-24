/**
 * @file OledPin.h
 * @brief Gestion simple de l'afficheur OLED pour afficher le code PIN.
 */

#pragma once

#include <Arduino.h>
#include <U8g2lib.h>
#include <vector>

class IOBase;

namespace OledPin {
  /** Initialise l'écran OLED (SSD1306 128x64 I2C). */
  void begin();
  /** Affiche des informations de statut (WiFi, services) pendant le boot. */
  void showStatus(const String& wifi,
                  const String& wifiHardware,
                  const String& web,
                  const String& udp);
  /** Met à jour l'affichage du statut (scrolling). */
  void loop();
  /** Ajoute un message d'erreur à faire défiler. */
  void pushErrorMessage(const String& message);
  /** Affiche le code PIN à l'écran. */
  void showPIN(int pin);
  /** Affiche un aperçu des IO après authentification. */
  void showIOValues(const std::vector<IOBase*>& ios);
}

/**
 * @file OledPin.h
 * @brief Gestion simple de l'afficheur OLED pour afficher le code PIN.
 */

#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

namespace OledPin {
  /** Initialise l'écran OLED (SSD1306 128x64 I2C). */
  void begin();
  /** Affiche des informations de statut (WiFi, services) pendant le boot. */
  void showStatus(const String& wifi, const String& web, const String& udp);
  /** Affiche le code PIN à l'écran. */
  void showPIN(int pin);
}

/**
 * @file OledPin.cpp
 * @brief Implémentation de l'afficheur OLED pour afficher le code PIN.
 */

#include "OledPin.h"

// Définit un écran SSD1306 128x64 I2C en mode matériel.  Les broches
// d'horloge (SCL) et de données (SDA) sont fixées à 14 et 12 pour
// correspondre au schéma fourni par l'utilisateur.
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C _oled(
    U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ 14, /* data=*/ 12);

void OledPin::begin() {
  _oled.begin();
  _oled.setPowerSave(0);
}

void OledPin::showStatus(const String& wifi, const String& web, const String& udp) {
  _oled.clearBuffer();
  _oled.setFont(u8g2_font_6x12_tf);
  _oled.drawStr(0, 12, wifi.c_str());
  _oled.drawStr(0, 26, web.c_str());
  _oled.drawStr(0, 40, udp.c_str());
  _oled.sendBuffer();
}

void OledPin::showPIN(int pin) {
  _oled.clearBuffer();
  _oled.setFont(u8g2_font_ncenB14_tr);
  _oled.drawStr(0, 16, "-Code PIN-");
  char buf[5];
  snprintf(buf, sizeof(buf), "%04d", pin);
  _oled.setFont(u8g2_font_fub30_tn);
  // Centrer le code sur l'écran
  int16_t x = 64 - (_oled.getStrWidth(buf) / 2);
  _oled.drawStr(x, 58, buf);
  _oled.sendBuffer();
}
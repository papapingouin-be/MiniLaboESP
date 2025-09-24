/**
 * @file OledPin.cpp
 * @brief Implémentation de l'afficheur OLED pour afficher le code PIN.
 */

#include "OledPin.h"
#include "core/IORegistry.h"

#include <deque>

// Définit un écran SSD1306 128x64 I2C en mode matériel.  Les broches
// d'horloge (SCL) et de données (SDA) sont fixées à 14 et 12 pour
// correspondre au schéma fourni par l'utilisateur.
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C _oled(
    U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ 14, /* data=*/ 12);

namespace {
  static const unsigned long SCROLL_INTERVAL_MS = 120;
  static const int16_t       SCROLL_GAP         = 24;
  static const size_t        MAX_ERROR_MESSAGES = 5;

  bool _statusActive = false;
  bool _statusDirty = false;
  String _wifiLine;
  String _wifiHardwareLine;
  String _webLine;
  String _udpLine;
  bool _pinVisible = false;
  String _pinDigits;

  std::deque<String> _errorMessages;
  String _scrollText;
  int16_t _scrollOffset = 0;
  int16_t _scrollWidth = 0;
  unsigned long _lastScrollTick = 0;

  void rebuildScrollText() {
    _scrollText = "";
    bool first = true;
    for (const auto& msg : _errorMessages) {
      if (!first) {
        _scrollText += "   |   ";
      }
      _scrollText += msg;
      first = false;
    }
    _scrollOffset = 0;
    _scrollWidth = 0;
    _statusDirty = true;
  }

  void renderStatus() {
    if (!_statusActive) {
      _statusDirty = false;
      return;
    }
    _oled.clearBuffer();
    _oled.setFont(u8g2_font_6x12_tf);
    _oled.drawStr(0, 12, _wifiLine.c_str());
    _oled.drawStr(0, 26, _wifiHardwareLine.c_str());
    _oled.drawStr(0, 40, _webLine.c_str());
    _oled.drawStr(0, 54, _udpLine.c_str());

    if (_pinVisible) {
      _oled.setFont(u8g2_font_6x12_tf);
      _oled.drawStr(84, 12, "PIN");
      _oled.setFont(u8g2_font_logisoso16_tn);
      int16_t pinWidth = _oled.getStrWidth(_pinDigits.c_str());
      int16_t baseX = 84 + (44 - pinWidth) / 2;
      if (baseX < 84) {
        baseX = 84;
      }
      _oled.drawStr(baseX, 60, _pinDigits.c_str());
      _oled.setFont(u8g2_font_6x12_tf);
    }

    const int baseline = 62;
    if (_scrollText.length() > 0) {
      _scrollWidth = _oled.getStrWidth(_scrollText.c_str());
      if (_scrollWidth <= 128) {
        _oled.drawStr(0, baseline, _scrollText.c_str());
      } else {
        int16_t step = _scrollWidth + SCROLL_GAP;
        int16_t baseX = -_scrollOffset;
        for (int offset = -1; offset <= 1; ++offset) {
          int16_t drawX = baseX + offset * step;
          _oled.drawStr(drawX, baseline, _scrollText.c_str());
        }
      }
    } else {
      _scrollWidth = 0;
      _scrollOffset = 0;
      _oled.drawStr(0, baseline, "Logs: OK");
    }

    _oled.sendBuffer();
    _statusDirty = false;
  }
}

void OledPin::begin() {
  _oled.begin();
  _oled.setPowerSave(0);
  _statusActive = false;
  _statusDirty = false;
  _errorMessages.clear();
  _scrollText.clear();
  _scrollOffset = 0;
  _scrollWidth = 0;
  _lastScrollTick = millis();
  _pinVisible = false;
  _pinDigits = "0000";
}

void OledPin::showStatus(const String& wifi,
                         const String& wifiHardware,
                         const String& web,
                         const String& udp) {
  bool wasActive = _statusActive;
  _statusActive = true;

  if (_wifiLine != wifi) {
    _wifiLine = wifi;
    _statusDirty = true;
  }
  if (_wifiHardwareLine != wifiHardware) {
    _wifiHardwareLine = wifiHardware;
    _statusDirty = true;
  }
  if (_webLine != web) {
    _webLine = web;
    _statusDirty = true;
  }
  if (_udpLine != udp) {
    _udpLine = udp;
    _statusDirty = true;
  }
  if (!wasActive) {
    _statusDirty = true;
  }

  if (_statusDirty) {
    renderStatus();
  }
}

void OledPin::loop() {
  if (!_statusActive) {
    return;
  }

  bool needRedraw = _statusDirty;
  unsigned long now = millis();
  if (_scrollText.length() > 0 && _scrollWidth > 128) {
    if (now - _lastScrollTick >= SCROLL_INTERVAL_MS) {
      _lastScrollTick = now;
      int16_t step = _scrollWidth + SCROLL_GAP;
      if (step <= 0) {
        step = 1;
      }
      _scrollOffset = (_scrollOffset + 1) % step;
      needRedraw = true;
    }
  } else if (_scrollOffset != 0) {
    _scrollOffset = 0;
    needRedraw = true;
  }

  if (needRedraw) {
    renderStatus();
  }
}

void OledPin::pushErrorMessage(const String& message) {
  String trimmed = message;
  trimmed.trim();
  if (trimmed.length() == 0) {
    return;
  }

  while (_errorMessages.size() >= MAX_ERROR_MESSAGES) {
    _errorMessages.pop_front();
  }
  _errorMessages.push_back(trimmed);
  rebuildScrollText();

  if (_statusActive) {
    renderStatus();
  }
}


void OledPin::showPIN(int pin) {
  _statusActive = false;
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

void OledPin::setPinCode(int pin) {
  if (pin < 0) {
    pin = 0;
  }
  if (pin > 9999) {
    pin = pin % 10000;
  }
  char buf[5];
  snprintf(buf, sizeof(buf), "%04d", pin);
  _pinDigits = String(buf);
  _pinVisible = true;
  _statusDirty = true;
  if (_statusActive) {
    renderStatus();
  }
}

void OledPin::showIOValues(const std::vector<IOBase*>& ios) {
  _statusActive = false;
  _oled.clearBuffer();
  _oled.setFont(u8g2_font_6x12_tf);
  _oled.drawStr(0, 12, "IO en temps reel:");
  int y = 26;
  size_t maxCount = ios.size();
  if (maxCount > 4) {
    maxCount = 4;
  }
  if (maxCount == 0) {
    _oled.drawStr(0, 26, "Aucune IO");
  } else {
    for (size_t i = 0; i < maxCount; ++i) {
      IOBase* io = ios[i];
      if (!io) continue;
      char line[32];
      float value = io->readRaw();
      snprintf(line, sizeof(line), "%s:%0.3f", io->id().c_str(), value);
      _oled.drawStr(0, y, line);
      y += 14;
    }
  }
  _oled.sendBuffer();
}

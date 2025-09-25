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
  bool _hasSessionPin = false;
  bool _hasExpectedPin = false;
  bool _hasSubmittedPin = false;
  String _sessionPinDigits;
  String _expectedPinDigits;
  String _submittedPinDigits;
  String _testStatusLine;

  constexpr const char kUnknownPin[] = "----";

  String formatPinDigits(int pin) {
    if (pin < 0) {
      pin = 0;
    }
    pin %= 10000;
    char buf[5];
    snprintf(buf, sizeof(buf), "%04d", pin);
    return String(buf);
  }

  String sanitizePinDigits(const String& value, bool& valid) {
    valid = false;
    if (value.length() == 4) {
      bool digitsOnly = true;
      for (size_t i = 0; i < 4; ++i) {
        char c = value.charAt(i);
        if (c < '0' || c > '9') {
          digitsOnly = false;
          break;
        }
      }
      if (digitsOnly) {
        valid = true;
        return value;
      }
    }

    String digits;
    digits.reserve(4);
    for (size_t i = 0; i < value.length() && digits.length() < 4; ++i) {
      char c = value.charAt(i);
      if (c >= '0' && c <= '9') {
        digits += c;
      }
    }

    if (digits.length() == 0) {
      return String(kUnknownPin);
    }

    while (digits.length() < 4) {
      digits = String('0') + digits;
    }
    valid = true;
    return digits;
  }

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

    if (_pinVisible) {
      auto drawLabelValue = [&](int y, const char* label, const String& value) {
        _oled.setFont(u8g2_font_6x12_tf);
        _oled.drawUTF8(0, y, label);
        if (value.length() > 0) {
          int16_t valueWidth = _oled.getUTF8Width(value.c_str());
          int16_t valueX = 128 - valueWidth;
          if (valueX < 72) {
            valueX = 72;
          }
          _oled.drawUTF8(valueX, y, value.c_str());
        }
      };

      drawLabelValue(12, "Code pin généré", _sessionPinDigits);
      drawLabelValue(24, "code pin envoyé", _submittedPinDigits);

      _oled.setFont(u8g2_font_6x12_tf);
      _oled.drawUTF8(0, 36, "lors du login par");
      _oled.drawUTF8(0, 48, "la page web");

      drawLabelValue(60, "etat du test", _testStatusLine);

      _oled.sendBuffer();
      _statusDirty = false;
      return;
    }

    _oled.setFont(u8g2_font_6x12_tf);
    _oled.drawStr(0, 12, _wifiLine.c_str());
    _oled.drawStr(0, 26, _wifiHardwareLine.c_str());
    _oled.drawStr(0, 40, _webLine.c_str());
    _oled.drawStr(0, 54, _udpLine.c_str());

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
  _hasSessionPin = false;
  _hasExpectedPin = false;
  _hasSubmittedPin = false;
  _sessionPinDigits = String(kUnknownPin);
  _expectedPinDigits = String(kUnknownPin);
  _submittedPinDigits = String(kUnknownPin);
  _testStatusLine = F("---");
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
  _oled.drawStr(0, 16, "-Codes PIN-");

  String sessionDigits = _hasSessionPin ? _sessionPinDigits : formatPinDigits(pin);
  String expectedDigits = _hasExpectedPin ? _expectedPinDigits : String(kUnknownPin);

  _oled.setFont(u8g2_font_6x12_tf);
  _oled.drawStr(0, 36, "Session :");
  _oled.drawStr(84, 36, sessionDigits.c_str());
  _oled.drawStr(0, 54, "Serveur :");
  _oled.drawStr(84, 54, expectedDigits.c_str());
  _oled.sendBuffer();
}

void OledPin::setSessionPin(int pin) {
  String digits = formatPinDigits(pin);
  bool prevVisible = _pinVisible;
  bool changed = !_hasSessionPin || _sessionPinDigits != digits;
  _sessionPinDigits = digits;
  _hasSessionPin = true;
  if (changed) {
    _submittedPinDigits = String(kUnknownPin);
    _hasSubmittedPin = false;
    _testStatusLine = F("---");
  }
  _pinVisible = _hasSessionPin || _hasExpectedPin || _hasSubmittedPin;
  if (changed || _pinVisible != prevVisible) {
    _statusDirty = true;
  }
  if (_statusActive && _statusDirty) {
    renderStatus();
  }
}

void OledPin::setExpectedPin(const String& pin) {
  bool valid = false;
  String digits = sanitizePinDigits(pin, valid);
  bool prevVisible = _pinVisible;
  bool changed = !_hasExpectedPin || _expectedPinDigits != digits;
  _expectedPinDigits = digits;
  _hasExpectedPin = valid;
  _pinVisible = _hasSessionPin || _hasExpectedPin || _hasSubmittedPin;
  if (changed || _pinVisible != prevVisible) {
    _statusDirty = true;
  }
  if (_statusActive && _statusDirty) {
    renderStatus();
  }
}

void OledPin::setPinCode(int pin) {
  setSessionPin(pin);
}

void OledPin::setSubmittedPin(const String& pin) {
  String digits;
  digits.reserve(4);
  for (size_t i = 0; i < pin.length() && digits.length() < 4; ++i) {
    char c = pin.charAt(i);
    if (c >= '0' && c <= '9') {
      digits += c;
    }
  }

  bool hasDigits = digits.length() > 0;
  String display;
  if (!hasDigits) {
    display = String(kUnknownPin);
  } else {
    display = digits;
    while (display.length() < 4) {
      display += '_';
    }
  }

  bool prevVisible = _pinVisible;
  bool changed = !_hasSubmittedPin || _submittedPinDigits != display;
  _submittedPinDigits = display;
  _hasSubmittedPin = hasDigits;
  _pinVisible = _hasSessionPin || _hasExpectedPin || _hasSubmittedPin;
  if (changed || _pinVisible != prevVisible) {
    _statusDirty = true;
  }
  if (_statusActive && _statusDirty) {
    renderStatus();
  }
}

void OledPin::setTestStatus(const String& status) {
  String sanitized = status;
  sanitized.replace('\r', ' ');
  sanitized.replace('\n', ' ');
  sanitized.trim();
  if (sanitized.length() == 0) {
    sanitized = F("---");
  }
  if (sanitized.length() > 18) {
    sanitized = sanitized.substring(0, 18);
  }

  if (_testStatusLine != sanitized) {
    _testStatusLine = sanitized;
    _statusDirty = true;
  }
  if (_statusActive && _statusDirty) {
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

/**
 * @file Logger.cpp
 * @brief Implémentation du système de journalisation MiniLabo.
 */

#include "Logger.h"

Logger::LogEntry Logger::_ring[RING_SIZE];
size_t Logger::_ringHead = 0;
bool Logger::_hasWrapped = false;
unsigned long Logger::_lastFlush = 0;
void (*Logger::_callback)(const String& line) = nullptr;

void Logger::begin() {
  _ringHead = 0;
  _hasWrapped = false;
  _lastFlush = millis();
}

void Logger::loop() {
  unsigned long now = millis();
  if (now - _lastFlush > FLUSH_INTERVAL) {
    flushToFS();
    _lastFlush = now;
  }
}

void Logger::setLogCallback(void (*callback)(const String& line)) {
  _callback = callback;
}

void Logger::log(LogLevel level, const String& category, const String& function, const String& message) {
  // Stockage en RAM
  LogEntry &e = _ring[_ringHead];
  e.ts = millis();
  e.level = level;
  e.category = category;
  e.function = function;
  e.message = message;
  _ringHead = (_ringHead + 1) % RING_SIZE;
  if (_ringHead == 0) _hasWrapped = true;

  // Formatage de la ligne
  char buf[32];
  snprintf(buf, sizeof(buf), "%10lu", e.ts);
  String line = String(buf) + " [" + levelToString(level) + "] " + e.category + "/" + e.function + ": " + e.message;
  // Appel du callback temps réel
  if (_callback) {
    _callback(line);
  }
  // Optionnel : impression série pour debug
  Serial.println(line);
}

void Logger::debug(const String& category, const String& function, const String& message) {
  log(LOG_DEBUG, category, function, message);
}
void Logger::info(const String& category, const String& function, const String& message) {
  log(LOG_INFO, category, function, message);
}
void Logger::warn(const String& category, const String& function, const String& message) {
  log(LOG_WARN, category, function, message);
}
void Logger::error(const String& category, const String& function, const String& message) {
  log(LOG_ERROR, category, function, message);
}

String Logger::tail(size_t n) {
  String result;
  if (!_hasWrapped && _ringHead == 0) return result;
  size_t count = _hasWrapped ? RING_SIZE : _ringHead;
  if (n > count) n = count;
  // Détermine la position du premier élément à retourner
  size_t start = (_ringHead + RING_SIZE - n) % RING_SIZE;
  for (size_t i = 0; i < n; ++i) {
    size_t idx = (start + i) % RING_SIZE;
    LogEntry &e = _ring[idx];
    char buf[32];
    snprintf(buf, sizeof(buf), "%10lu", e.ts);
    result += String(buf) + " [" + levelToString(e.level) + "] " + e.category + "/" + e.function + ": " + e.message + "\n";
  }
  return result;
}

String Logger::levelToString(LogLevel level) {
  switch (level) {
    case LOG_DEBUG: return "D";
    case LOG_INFO:  return "I";
    case LOG_WARN:  return "W";
    case LOG_ERROR: return "E";
    default: return "?";
  }
}

void Logger::flushToFS() {
  // Ouvre le fichier courant en append
  File f = LittleFS.open("/logs/log_current.log", "a");
  if (!f) {
    // Crée le répertoire logs si besoin
    LittleFS.mkdir("/logs");
    f = LittleFS.open("/logs/log_current.log", "a");
    if (!f) {
      // Impossible de créer le fichier
      return;
    }
  }
  // Écrire toutes les entrées depuis le dernier flush
  // Comme le ring est partagé, on écrit tout (simple implémentation)
  size_t count = _hasWrapped ? RING_SIZE : _ringHead;
  for (size_t i = 0; i < count; ++i) {
    LogEntry &e = _ring[i];
    char buf[32];
    snprintf(buf, sizeof(buf), "%10lu", e.ts);
    String line = String(buf) + " [" + levelToString(e.level) + "] " + e.category + "/" + e.function + ": " + e.message + "\n";
    f.print(line);
  }
  f.flush();
  size_t size = f.size();
  f.close();
  // Rotation basique si le fichier est trop gros
  if (size > FILE_MAX_SIZE) {
    // Renommer avec timestamp
    String newName = String("/logs/log_") + String(millis()) + String(".log");
    LittleFS.rename("/logs/log_current.log", newName.c_str());
  }
}

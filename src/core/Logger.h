/**
 * @file Logger.h
 * @brief Gestion d'un système de journalisation léger pour MiniLabo.
 *
 * Le Logger fournit des fonctions de log de différents niveaux (debug,
 * info, warning, error) en enregistrant les messages dans un tampon
 * circulaire en RAM puis en les écrivant périodiquement dans un
 * fichier sur LittleFS.  Il est possible de brancher un callback
 * externe afin de diffuser les messages en temps réel (par exemple
 * vers un WebSocket).
 */

#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <vector>

/**
 * Niveaux de journalisation.
 */
enum LogLevel {
  LOG_DEBUG = 0,
  LOG_INFO  = 1,
  LOG_WARN  = 2,
  LOG_ERROR = 3
};

class Logger {
public:
  /** Initialise le logger et crée le fichier courant si nécessaire. */
  static void begin();
  /** Appelé périodiquement pour vider le tampon vers la flash. */
  static void loop();
  /** Enregistre un message avec un niveau et une catégorie. */
  static void log(LogLevel level, const String& category, const String& function, const String& message);
  /** Enregistre un message au niveau DEBUG. */
  static void debug(const String& category, const String& function, const String& message);
  /** Enregistre un message au niveau INFO. */
  static void info(const String& category, const String& function, const String& message);
  /** Enregistre un message au niveau WARN. */
  static void warn(const String& category, const String& function, const String& message);
  /** Enregistre un message au niveau ERROR. */
  static void error(const String& category, const String& function, const String& message);

  /**
   * Enregistre un callback appelé à chaque message loggé. Plusieurs
   * callbacks peuvent être enregistrés ; ils seront invoqués dans l'ordre
   * d'inscription avec la ligne déjà formatée (timestamp et texte).
   */
  static void setLogCallback(void (*callback)(const String& line));

  /**
   * Retourne les dernières lignes du journal en RAM (tampon circulaire).
   * Le paramètre n limite le nombre de lignes retournées.
   */
  static String tail(size_t n);

private:
  static const size_t RING_SIZE = 200;         ///< Nombre d'entrées en RAM
  static const unsigned long FLUSH_INTERVAL = 5000UL; ///< Période de flush (ms)
  static const size_t FILE_MAX_SIZE = 65536;    ///< Taille max du fichier courant (octets)

  struct LogEntry {
    unsigned long ts;
    LogLevel level;
    String category;
    String function;
    String message;
  };

  static LogEntry _ring[RING_SIZE];
  static size_t _ringHead;
  static bool _hasWrapped;
  static unsigned long _lastFlush;
  static std::vector<void (*)(const String& line)> _callbacks;

  static void flushToFS();
  static String levelToString(LogLevel level);
};

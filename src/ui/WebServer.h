/**
 * @file WebServer.h
 * @brief Définition du serveur web asynchrone pour MiniLabo.
 */

#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

class WebServer {
public:
  /** Initialise et démarre le serveur web (routes REST, websockets). */
  static bool begin();
  /** Indique si le serveur web est démarré. */
  static bool isStarted();
  /** Retourne le port d'écoute HTTP. */
  static uint16_t port();
  /** Indique si un client s'est authentifié avec succès. */
  static bool hasAuthenticatedClient();
  /** Boucle d'entretien (aucune action nécessaire actuellement). */
  static void loop() {}
private:
  static AsyncWebServer _server;
  static AsyncWebSocket _wsLogs;
  static int _logClients;
  static bool _started;
  static bool _hasAuthenticatedClient;
  static void logCallback(const String& line);
  static bool checkAuth(AsyncWebServerRequest *request);
};

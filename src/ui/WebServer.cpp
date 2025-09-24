/**
 * @file WebServer.cpp
 * @brief ImplÃƒÂ©mentation du serveur web asynchrone MiniLabo.
 */

#include "WebServer.h"
#include "core/ConfigStore.h"
#include "core/Logger.h"
#include "core/IORegistry.h"
#include "devices/DMM.h"
#include "devices/Scope.h"
#include "devices/FuncGen.h"

#include <ArduinoJson.h>

// DÃƒÂ©claration des membres statiques
AsyncWebServer WebServer::_server(80);
AsyncWebSocket WebServer::_wsLogs("/ws/logs");
int WebServer::_logClients = 0;
bool WebServer::_started = false;
bool WebServer::_hasAuthenticatedClient = false;

bool WebServer::begin() {
  _started = false;
  _hasAuthenticatedClient = false;
  // Initialise les appareils (multimÃƒÂ¨tre, oscilloscope, gÃƒÂ©nÃƒÂ©rateur)
  DMM::begin();
  Scope::begin();
  FuncGen::begin();

  // Initialise le callback de log pour diffusion en temps rÃƒÂ©el
  Logger::setLogCallback(logCallback);

  // Gestion des WebSockets
  _wsLogs.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client,
                     AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
      _logClients++;
      Logger::info("WS", "connect", String("Client logs connected: ") + _logClients);
    } else if (type == WS_EVT_DISCONNECT) {
      _logClients--;
      if (_logClients < 0) _logClients = 0;
      Logger::info("WS", "disconnect", String("Client logs disconnected: ") + _logClients);
    }
  });
  _server.addHandler(&_wsLogs);

  // Route statique pour servir les fichiers depuis LittleFS
  _server.serveStatic("/", LittleFS, "/").setDefaultFile("/index.html");

  // Route POST /login
  _server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Lire le corps JSON
    if (request->hasArg("body")) {
      String body = request->arg("body");
      DynamicJsonDocument doc(128);
      DeserializationError err = deserializeJson(doc, body);
      if (err) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
        return;
      }
      String pinStr = doc["pin"].as<String>();
      int pinVal = pinStr.toInt();
      auto& gdoc = ConfigStore::doc("general");
      int stored = gdoc["pin"].as<int>();
      if (pinVal == stored) {
        // Auth ok : dÃƒÂ©finir cookie
        _hasAuthenticatedClient = true;
        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"success\":true}");
        response->addHeader("Set-Cookie", String("mlpin=1; Path=/"));
        request->send(response);
        return;
      }
    }
    request->send(401, "application/json", "{\"success\":false,\"error\":\"PIN incorrect\"}");
  });

  // Route GET /api/io
  _server.on("/api/io", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!checkAuth(request)) {
      request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    DynamicJsonDocument doc(1024);
    JsonArray arr = doc.to<JsonArray>();
    auto list = IORegistry::list();
    for (auto io : list) {
      JsonObject obj = arr.add<JsonObject>();
      obj["id"] = io->id();
      obj["raw"] = io->readRaw();
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  // Route GET /api/dmm
  _server.on("/api/dmm", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!checkAuth(request)) {
      request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    DMM::loop(); // mise ÃƒÂ  jour rapide
    DynamicJsonDocument doc(256);
    JsonObject obj = doc.to<JsonObject>();
    DMM::values(obj);
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  // Route GET /api/scope
  _server.on("/api/scope", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!checkAuth(request)) {
      request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    // Mise ÃƒÂ  jour de l'oscilloscope
    Scope::loop();
    DynamicJsonDocument doc(1024);
    JsonObject obj = doc.to<JsonObject>();
    Scope::toJson(obj);
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  // Route POST /api/funcgen
  _server.on("/api/funcgen", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!checkAuth(request)) {
      request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    if (!request->hasArg("body")) {
      request->send(400, "application/json", "{\"error\":\"Missing body\"}");
      return;
    }
    String body = request->arg("body");
    DynamicJsonDocument doc(256);
    if (deserializeJson(doc, body)) {
      request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    String target = doc["target"].as<String>();
    float freq = doc["freq"].as<float>();
    float amp = doc["amplitude"].as<float>();
    float off = doc["offset"].as<float>();
    String wave = doc["wave"].as<String>();
    FuncGen::updateTarget(target, freq, amp, off, wave);
    request->send(200, "application/json", "{\"success\":true}");
  });

  // Route GET /api/logs/tail
  _server.on("/api/logs/tail", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!checkAuth(request)) {
      request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    size_t lines = 50;
    if (request->hasParam("n")) {
      lines = request->getParam("n")->value().toInt();
    }
    String result = Logger::tail(lines);
    DynamicJsonDocument doc(256);
    doc["lines"] = result;
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });


  // Route générique pour /api/config/<area>
  _server.onNotFound([](AsyncWebServerRequest *request) {
    String url = request->url();
    if (!url.startsWith("/api/config/")) {
      request->send(404, "text/plain", "Not found");
      return;
    }
    if (!checkAuth(request)) {
      request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    String area = url.substring(strlen("/api/config/"));
    bool exists = false;
    static const char* areas[] = {"general","network","io","dmm","scope","funcgen","math"};
    for (auto a : areas) { if (area == a) { exists = true; break; } }
    if (!exists) {
      request->send(404, "application/json", "{\"error\":\"Unknown area\"}");
      return;
    }
    if (request->method() == HTTP_GET) {
      JsonDocument &cfg = ConfigStore::doc(area);
      String out;
      serializeJson(cfg, out);
      request->send(200, "application/json", out);
      return;
    }
    if (request->method() != HTTP_PUT && request->method() != HTTP_POST) {
      request->send(405, "application/json", "{\"error\":\"Method Not Allowed\"}");
      return;
    }
    if (!request->hasArg("body")) {
      request->send(400, "application/json", "{\"error\":\"Missing body\"}");
      return;
    }
    String body = request->arg("body");
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, body)) {
      request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    JsonDocument &cfg = ConfigStore::doc(area);
    cfg.clear();
    cfg = doc;
    ConfigStore::requestSave(area);
    if (area == "io") {
      IORegistry::begin();
    } else if (area == "dmm") {
      DMM::begin();
    } else if (area == "scope") {
      Scope::begin();
    } else if (area == "funcgen") {
      FuncGen::begin();
    }
    request->send(200, "application/json", "{\"success\":true}");
  });
  // DÃƒÂ©marre le serveur
  _server.begin();
  Logger::info("WS", "begin", "Web server started on port 80");
  _started = true;
  return true;
}

bool WebServer::isStarted() {
  return _started;
}

uint16_t WebServer::port() {
  return 80;
}

bool WebServer::hasAuthenticatedClient() {
  return _hasAuthenticatedClient;
}

bool WebServer::checkAuth(AsyncWebServerRequest *request) {
  // VÃƒÂ©rifie la prÃƒÂ©sence du cookie 'mlpin=1'.  Sans cookie, on autorise
  // l'accÃƒÂ¨s aux endpoints publics (ex. index.html).  Les appels API
  // exigent l'authentification via ce cookie.
  if (!request->hasHeader("Cookie")) return false;
  String cookie = request->header("Cookie");
  // Recherche mlpin=1
  int idx = cookie.indexOf("mlpin=");
  if (idx < 0) return false;
  // Extrait la valeur
  String val = cookie.substring(idx + 6);
  int end = val.indexOf(';');
  if (end >= 0) val = val.substring(0, end);
  val.trim();
  return val == "1";
}

void WebServer::logCallback(const String& line) {
  // Diffuse la ligne sur les clients WebSocket actifs
  if (_logClients > 0) {
    _wsLogs.textAll(line);
  }
}

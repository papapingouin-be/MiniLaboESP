/**
 * @file WebServer.cpp
 * @brief ImplÃƒÂ©mentation du serveur web asynchrone MiniLabo.
 */

#include "WebServer.h"
#include "core/ConfigStore.h"
#include "core/Logger.h"
#include "core/IORegistry.h"
#include "OledPin.h"
#include "devices/DMM.h"
#include "devices/Scope.h"
#include "devices/FuncGen.h"

#include <ArduinoJson.h>
#include <pgmspace.h>

namespace {
constexpr const char kDefaultIndexHtml[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8">
  <title>MiniLabo Dashboard</title>
  <style>
    body { background:#f4f6fb; color:#1f2933; font-family:Arial,Helvetica,sans-serif; margin:0; }
    header { padding:1.25rem; text-align:center; font-size:1.6rem; background:#ffffff; color:#1f2933; border-bottom:1px solid #d0d7de; box-shadow:0 2px 6px rgba(15,23,42,0.08); }
    #loginForm { width:320px; margin:4rem auto; padding:2.25rem; background:#ffffff; color:#1f2933; border-radius:0.85rem; box-shadow:0 18px 40px rgba(15,23,42,0.18); }
    #loginForm input { width:100%; padding:0.65rem; margin:0.5rem 0; background:#f9fafc; border:1px solid #cbd5e1; border-radius:0.55rem; color:#1f2933; box-shadow:inset 0 1px 2px rgba(15,23,42,0.05); }
    #loginForm .numpad { display:grid; grid-template-columns:repeat(3,1fr); gap:0.6rem; margin-top:1.2rem; }
    #loginForm .numpad button { background:#e2e8f0; color:#1f2933; padding:0.85rem; font-size:1.25rem; border:none; border-radius:0.6rem; transition:background 0.2s ease, transform 0.1s ease; }
    #loginForm .numpad button:hover { background:#cbd5f5; transform:translateY(-1px); }
    #loginForm .numpad-actions { display:flex; gap:0.6rem; margin-top:0.6rem; }
    #loginForm .numpad-actions button { flex:1; background:#dbeafe; color:#1f2937; border:none; border-radius:0.6rem; }
    #dashboard { display:none; padding:1.5rem; }
    .cards { display:flex; flex-wrap:wrap; gap:1.25rem; }
    .card { background:#ffffff; border:1px solid #e2e8f0; border-radius:0.85rem; padding:1.25rem; flex:1; min-width:250px; box-shadow:0 12px 30px rgba(15,23,42,0.12); }
    button { background:#2563eb; color:#fff; border:none; padding:0.6rem 1.2rem; border-radius:0.5rem; cursor:pointer; transition:background 0.2s ease, transform 0.1s ease; }
    button:hover { background:#1d4ed8; transform:translateY(-1px); }
    input, select { background:#f9fafc; border:1px solid #cbd5e1; color:#1f2933; border-radius:0.5rem; padding:0.4rem 0.5rem; box-shadow:inset 0 1px 2px rgba(15,23,42,0.05); }
    #logsPanel { background:#f9fafc; border:1px solid #d0d7de; max-height:220px; overflow:auto; padding:0.75rem; margin-top:1rem; border-radius:0.6rem; box-shadow:inset 0 1px 3px rgba(15,23,42,0.1); }
  </style>
</head>
<body>
  <header>MiniLabo</header>
  <div id="loginForm">
    <h2>Connexion</h2>
    <label for="pinInput">Code PIN&nbsp;:</label><br>
    <input type="password" id="pinInput" maxlength="4" inputmode="numeric" pattern="[0-9]*"><br>
    <div class="numpad">
      <button type="button" onclick="appendDigit('1')">1</button>
      <button type="button" onclick="appendDigit('2')">2</button>
      <button type="button" onclick="appendDigit('3')">3</button>
      <button type="button" onclick="appendDigit('4')">4</button>
      <button type="button" onclick="appendDigit('5')">5</button>
      <button type="button" onclick="appendDigit('6')">6</button>
      <button type="button" onclick="appendDigit('7')">7</button>
      <button type="button" onclick="appendDigit('8')">8</button>
      <button type="button" onclick="appendDigit('9')">9</button>
      <div></div>
      <button type="button" onclick="appendDigit('0')">0</button>
      <div></div>
    </div>
    <div class="numpad-actions">
      <button type="button" onclick="clearPin()">Effacer</button>
      <button type="button" onclick="backspacePin()">⌫</button>
    </div>
    <button type="button" onclick="login()">Se connecter</button>
    <p id="loginStatus" style="color:red;"></p>
  </div>
  <div id="dashboard">
    <div class="cards">
      <div class="card" id="cardDMM">
        <h3>Multimètre</h3>
        <div id="dmmValues">...</div>
        <div>
          Canal: <select id="dmmSelect"></select>
        </div>
      </div>
      <div class="card" id="cardScope">
        <h3>Oscilloscope</h3>
        <canvas id="scopeCanvas" width="300" height="150" style="background:#f1f5f9;border:1px solid #cbd5e1;border-radius:0.5rem;"></canvas>
        <p>En développement...</p>
      </div>
      <div class="card" id="cardFunc">
        <h3>Générateur de fonction</h3>
        <div>
          Cible: <select id="funcTarget"></select><br>
          Fréquence (Hz): <input type="number" id="funcFreq" value="50"><br>
          Amplitude (%): <input type="number" id="funcAmp" value="50"><br>
          Offset (%): <input type="number" id="funcOff" value="0"><br>
          Forme: <select id="funcWave">
            <option value="sine">Sinus</option>
            <option value="square">Carré</option>
            <option value="triangle">Triangle</option>
          </select><br>
          <button onclick="updateFunc()">Appliquer</button>
        </div>
      </div>
      <div class="card" id="cardIO">
        <h3>IO disponibles</h3>
        <ul id="ioList" style="list-style:none; padding-left:0;"></ul>
      </div>
    </div>
    <div>
      <button onclick="toggleLogs()" id="logsBtn">Afficher les logs</button>
    </div>
    <div id="logsPanel" style="display:none;"></div>
  </div>
  <script>
    const pinInput = document.getElementById('pinInput');

    function sanitizePin(value) {
      return (value || '').replace(/[^0-9]/g, '').slice(0, 4);
    }

    function appendDigit(digit) {
      pinInput.value = sanitizePin(pinInput.value + digit);
      pinInput.focus();
    }

    function clearPin() {
      pinInput.value = '';
      pinInput.focus();
    }

    function backspacePin() {
      pinInput.value = pinInput.value.slice(0, -1);
      pinInput.focus();
    }

    pinInput.addEventListener('input', (event) => {
      const sanitized = sanitizePin(event.target.value);
      if (sanitized !== event.target.value) {
        event.target.value = sanitized;
      }
    });

    pinInput.addEventListener('keyup', (event) => {
      if (event.key === 'Enter') {
        login();
      }
    });

    pinInput.focus();

    function login() {
      const pin = sanitizePin(pinInput.value);
      pinInput.value = pin;
      fetch('/login', {
        method:'POST',
        headers:{'Content-Type':'application/json'},
        body: JSON.stringify({pin:pin})
      }).then(r => r.json()).then(data => {
        if (data.success) {
          document.getElementById('loginForm').style.display='none';
          document.getElementById('dashboard').style.display='block';
          loadIO();
          loadFuncTargets();
          loadDmmChannels();
          startLogs();
        } else {
          document.getElementById('loginStatus').innerText='PIN incorrect';
        }
      }).catch(err => {
        document.getElementById('loginStatus').innerText='Erreur: '+err;
      });
    }

    function checkAuthResponse(resp) {
      if (resp.status === 401) {
        document.getElementById('loginForm').style.display='block';
        document.getElementById('dashboard').style.display='none';
        document.getElementById('loginStatus').innerText='Veuillez vous reconnecter';
        throw new Error('Unauthorized');
      }
      return resp;
    }

    function loadIO() {
      fetch('/api/io').then(checkAuthResponse).then(r => r.json()).then(data => {
        const list = document.getElementById('ioList');
        list.innerHTML='';
        (data || []).forEach(io => {
          const li = document.createElement('li');
          li.textContent = io.id+': '+io.raw;
          list.appendChild(li);
        });
      });
    }

    function loadDmmChannels() {
      fetch('/api/dmm').then(checkAuthResponse).then(r => r.json()).then(data => {
        const select = document.getElementById('dmmSelect');
        select.innerHTML='';
        if (data.channels) {
          data.channels.forEach((ch, idx) => {
            const opt = document.createElement('option');
            opt.value = idx;
            opt.textContent = ch.name;
            select.appendChild(opt);
          });
        }
        if (data.display) {
          document.getElementById('dmmValues').textContent = data.display;
        }
      });
    }

    function loadFuncTargets() {
      fetch('/api/config/funcgen').then(checkAuthResponse).then(r => r.json()).then(cfg => {
        const select = document.getElementById('funcTarget');
        select.innerHTML='';
        if (cfg.targets) {
          cfg.targets.forEach(t => {
            const opt = document.createElement('option');
            opt.value = t.id;
            opt.textContent = t.name || t.id;
            select.appendChild(opt);
          });
        }
      });
    }

    function updateFunc() {
      const payload = {
        target: document.getElementById('funcTarget').value,
        freq: parseFloat(document.getElementById('funcFreq').value),
        amplitude: parseFloat(document.getElementById('funcAmp').value),
        offset: parseFloat(document.getElementById('funcOff').value),
        wave: document.getElementById('funcWave').value
      };
      fetch('/api/funcgen', {
        method:'POST',
        headers:{'Content-Type':'application/json'},
        body: JSON.stringify(payload)
      }).then(checkAuthResponse).then(r => r.json()).then(resp => {
        if (!resp.success) {
          alert('Erreur lors de la mise à jour du générateur');
        }
      }).catch(err => alert('Erreur réseau: '+err));
    }

    let logsVisible = false;
    let ws;

    function toggleLogs() {
      logsVisible = !logsVisible;
      document.getElementById('logsPanel').style.display = logsVisible ? 'block' : 'none';
      document.getElementById('logsBtn').innerText = logsVisible ? 'Masquer les logs' : 'Afficher les logs';
      if (logsVisible) {
        startLogs();
      } else if (ws) {
        ws.close();
      }
    }

    function startLogs() {
      if (ws) {
        ws.close();
      }
      ws = new WebSocket('ws://'+window.location.host+'/ws/logs');
      ws.onmessage = (evt) => {
        const panel = document.getElementById('logsPanel');
        panel.textContent += evt.data+'\n';
        panel.scrollTop = panel.scrollHeight;
      };
      ws.onclose = () => { ws = null; };
    }

    setInterval(() => {
      if (document.getElementById('dashboard').style.display === 'block') {
        loadIO();
        loadDmmChannels();
      }
    }, 2000);
  </script>
</body>
</html>)rawliteral";

void ensureIndexHtmlPresent() {
  if (LittleFS.exists("/index.html")) {
    return;
  }
  File f = LittleFS.open("/index.html", "w");
  if (!f) {
    Logger::error("WS", "begin", "Failed to create /index.html");
    return;
  }
  f.print(FPSTR(kDefaultIndexHtml));
  f.close();
  Logger::info("WS", "begin", "Restored default /index.html");
}
}  // namespace

// DÃƒÂ©claration des membres statiques
AsyncWebServer WebServer::_server(80);
AsyncWebSocket WebServer::_wsLogs("/ws/logs");
int WebServer::_logClients = 0;
bool WebServer::_started = false;
bool WebServer::_hasAuthenticatedClient = false;

bool WebServer::begin() {
  ensureIndexHtmlPresent();
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

  // Gestion du corps des requêtes JSON (collecte dans request->_tempObject)
  _server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data,
                           size_t len, size_t index, size_t total) {
    if (request->method() != HTTP_POST && request->method() != HTTP_PUT) {
      return;
    }
    String *body = static_cast<String *>(request->_tempObject);
    if (index == 0 || body == nullptr) {
      body = new String();
      if (total > 0) {
        body->reserve(total);
      }
      request->_tempObject = body;
    }
    if (!body) {
      return;
    }
    body->concat(reinterpret_cast<const char *>(data), len);
  });

  // Route statique pour servir les fichiers depuis LittleFS
  _server.serveStatic("/", LittleFS, "/").setDefaultFile("/index.html");

  // Route POST /login
  _server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Lire le corps JSON
    String body = readRequestBody(request);
    if (body.length()) {
      DynamicJsonDocument doc(128);
      DeserializationError err = deserializeJson(doc, body);
      if (err) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
        return;
      }

      auto extractDigits = [](const String& value) {
        String digits;
        digits.reserve(4);
        for (size_t i = 0; i < value.length() && digits.length() < 4; ++i) {
          char c = value.charAt(i);
          if (c >= '0' && c <= '9') {
            digits += c;
          }
        }
        return digits;
      };

      auto normalizePin = [&extractDigits](const String& value) {
        String digits = extractDigits(value);
        if (digits.length() == 0) {
          digits = F("0000");
        }
        while (digits.length() < 4) {
          digits = String('0') + digits;
        }
        return digits;
      };

      String submittedSanitized = extractDigits(doc["pin"].as<String>());
      if (submittedSanitized.length() != 4) {
        request->send(401, "application/json", "{\"success\":false,\"error\":\"PIN incorrect\"}");
        return;
      }
      auto& gdoc = ConfigStore::doc("general");
      JsonVariant pinVariant = gdoc["pin"];
      String expectedRaw;
      if (pinVariant.isNull()) {
        expectedRaw = F("0000");
      } else if (pinVariant.is<const char*>()) {
        expectedRaw = pinVariant.as<const char*>();
      } else if (pinVariant.is<int>()) {
        expectedRaw = String(pinVariant.as<int>());
      } else if (pinVariant.is<long>()) {
        expectedRaw = String(pinVariant.as<long>());
      } else {
        expectedRaw = pinVariant.as<String>();
      }
      String expectedSanitized = normalizePin(expectedRaw);

      if (expectedSanitized.length() == 4) {
        OledPin::pushErrorMessage(String(F("PIN cfg=")) + expectedSanitized +
                                  F(" login=") + submittedSanitized);
      }

      if (expectedSanitized.length() == 4 &&
          submittedSanitized == expectedSanitized) {
        // Auth ok : définir cookie
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
    String body = readRequestBody(request);
    if (!body.length()) {
      request->send(400, "application/json", "{\"error\":\"Missing body\"}");
      return;
    }
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
    String body = readRequestBody(request);
    if (!body.length()) {
      request->send(400, "application/json", "{\"error\":\"Missing body\"}");
      return;
    }
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

String WebServer::readRequestBody(AsyncWebServerRequest *request) {
  if (!request || !request->_tempObject) {
    return String();
  }
  String *body = static_cast<String *>(request->_tempObject);
  String result = *body;
  delete body;
  request->_tempObject = nullptr;
  return result;
}

void WebServer::logCallback(const String& line) {
  // Diffuse la ligne sur les clients WebSocket actifs
  if (_logClients > 0) {
    _wsLogs.textAll(line);
  }
}

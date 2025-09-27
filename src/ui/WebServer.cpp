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
    #debugToggle { display:inline-block; margin-top:0.75rem; color:#2563eb; text-decoration:none; font-size:0.95rem; }
    #debugToggle:hover { text-decoration:underline; }
    #debugPanel { display:none; margin-top:0.75rem; background:#f9fafc; border:1px solid #d0d7de; border-radius:0.6rem; padding:0.75rem; box-shadow:inset 0 1px 3px rgba(15,23,42,0.1); }
    #debugPanel h3 { margin-top:0; font-size:1rem; color:#1f2933; }
    #debugLog { max-height:160px; overflow:auto; background:#ffffff; border:1px solid #cbd5e1; border-radius:0.5rem; padding:0.5rem; font-size:0.85rem; line-height:1.3; }
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
    <button type="button" onclick="triggerOledTest()" style="margin-top:0.6rem;">Test OLED</button>
    <p id="loginStatus" style="color:red;"></p>
    <a href="#" id="debugToggle" onclick="toggleDebug(event)">Afficher le debug</a>
    <div id="debugPanel">
      <h3>Debug</h3>
      <pre id="debugLog"></pre>
    </div>
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
    const debugPanel = document.getElementById('debugPanel');
    const debugLog = document.getElementById('debugLog');
    const debugToggleLink = document.getElementById('debugToggle');
    let lastSentPin = '';
    let uiSocket = null;
    const pendingUiEvents = [];
    const UI_SOCKET_RETRY_MS = 3000;

    function flushPendingUiEvents() {
      if (!uiSocket || uiSocket.readyState !== WebSocket.OPEN) {
        return;
      }
      while (pendingUiEvents.length) {
        const payload = pendingUiEvents.shift();
        try {
          uiSocket.send(payload);
          try {
            const parsed = JSON.parse(payload);
            appendDebug(`event ws (retry) => ${parsed.type || '?'}`);
          } catch (_) {
            appendDebug('event ws (retry)');
          }
        } catch (err) {
          console.warn('ui socket retry error', err);
          appendDebug(`event ws retry erreur: ${err}`);
          pendingUiEvents.unshift(payload);
          break;
        }
      }
    }

    function ensureUiSocket() {
      if (uiSocket && (uiSocket.readyState === WebSocket.OPEN || uiSocket.readyState === WebSocket.CONNECTING)) {
        return;
      }
      connectUiSocket();
    }

    function connectUiSocket() {
      const protocol = window.location.protocol === 'https:' ? 'wss://' : 'ws://';
      try {
        uiSocket = new WebSocket(protocol + window.location.host + '/ws/ui');
      } catch (err) {
        console.warn('ui socket init error', err);
        appendDebug(`ui socket init erreur: ${err}`);
        uiSocket = null;
        return;
      }
      uiSocket.addEventListener('open', () => {
        appendDebug('UI socket connecté');
        flushPendingUiEvents();
      });
      uiSocket.addEventListener('message', (event) => {
        if (event && event.data) {
          appendDebug(`ui <= ${event.data}`);
        }
      });
      uiSocket.addEventListener('close', () => {
        appendDebug('UI socket fermé');
        uiSocket = null;
        setTimeout(ensureUiSocket, UI_SOCKET_RETRY_MS);
      });
      uiSocket.addEventListener('error', (event) => {
        console.warn('ui socket error', event);
        appendDebug('ui socket erreur');
      });
    }

    function toggleDebug(event) {
      if (event) {
        event.preventDefault();
      }
      if (!debugPanel || !debugToggleLink) {
        return false;
      }
      const isHidden = debugPanel.style.display === 'none' || debugPanel.style.display === '';
      debugPanel.style.display = isHidden ? 'block' : 'none';
      debugToggleLink.textContent = isHidden ? 'Masquer le debug' : 'Afficher le debug';
      if (!isHidden && debugLog) {
        debugLog.scrollTop = debugLog.scrollHeight;
      }
      return false;
    }

    function appendDebug(message) {
      if (!debugLog) {
        return;
      }
      const timestamp = new Date().toISOString();
      debugLog.textContent += `[${timestamp}] ${message}\n`;
      if (debugLog.textContent.length > 8000) {
        debugLog.textContent = debugLog.textContent.slice(debugLog.textContent.length - 8000);
      }
      debugLog.scrollTop = debugLog.scrollHeight;
    }

    ensureUiSocket();

    async function sendLoginEvent(type, details) {
      const payloadObj = Object.assign({type:type}, details || {});
      const payloadJson = JSON.stringify(payloadObj);
      const debugDetails = JSON.stringify(details || {});
      if (uiSocket && uiSocket.readyState === WebSocket.OPEN) {
        try {
          uiSocket.send(payloadJson);
          appendDebug(`event ws => ${type} ${debugDetails}`);
          return;
        } catch (err) {
          console.warn('ui socket send error', err);
          appendDebug(`event ws erreur => ${err}`);
        }
      }

      ensureUiSocket();

      try {
        const response = await fetch('/api/login/event', {
          method:'POST',
          headers:{'Content-Type':'application/json'},
          credentials:'same-origin',
          keepalive:true,
          cache:'no-store',
          body: payloadJson
        });
        appendDebug(`event http => ${type} ${debugDetails} status=${response.status}`);
        if (!response.ok) {
          throw new Error('HTTP '+response.status);
        }
      } catch (e) {
        console.warn('login event http error', e);
        appendDebug(`event http erreur => ${e}`);
        pendingUiEvents.push(payloadJson);
        if (pendingUiEvents.length > 20) {
          pendingUiEvents.shift();
        }
        flushPendingUiEvents();
      }
    }

    function notifyPinChange() {
      const pin = sanitizePin(pinInput.value);
      if (pin !== lastSentPin) {
        lastSentPin = pin;
        sendLoginEvent('pin_update', {pin: pin});
        appendDebug(`pin_update local => ${pin}`);
      }
    }

    function sanitizePin(value) {
      return (value || '').replace(/[^0-9]/g, '').slice(0, 4);
    }

    function appendDigit(digit) {
      pinInput.value = sanitizePin(pinInput.value + digit);
      pinInput.focus();
      notifyPinChange();
    }

    function clearPin() {
      pinInput.value = '';
      pinInput.focus();
      notifyPinChange();
    }

    function backspacePin() {
      pinInput.value = pinInput.value.slice(0, -1);
      pinInput.focus();
      notifyPinChange();
    }

    pinInput.addEventListener('input', (event) => {
      const sanitized = sanitizePin(event.target.value);
      if (sanitized !== event.target.value) {
        event.target.value = sanitized;
      }
      notifyPinChange();
    });

    pinInput.addEventListener('keyup', (event) => {
      if (event.key === 'Enter') {
        login();
      }
    });

    pinInput.focus();
    sendLoginEvent('page_load');
    notifyPinChange();

    function triggerOledTest() {
      sendLoginEvent('test_message', {message:'test'});
      if (typeof appendDebug === 'function') {
        appendDebug('test_message envoyé');
      }
    }

    function login() {
      const pin = sanitizePin(pinInput.value);
      pinInput.value = pin;
      fetch('/login', {
        method:'POST',
        headers:{'Content-Type':'application/json'},
        credentials:'same-origin',
        body: JSON.stringify({pin:pin})
      }).then(r => r.json()).then(data => {
        appendDebug(`login response => ${JSON.stringify(data)}`);
        if (data.success) {
          document.getElementById('loginForm').style.display='none';
          document.getElementById('dashboard').style.display='block';
          loadIO();
          loadFuncTargets();
          loadDmmChannels();
          startLogs();
          sendLoginEvent('login_result', {success:true, message:'Connexion OK', pin:pin});
        } else {
          document.getElementById('loginStatus').innerText='PIN incorrect';
          sendLoginEvent('login_result', {success:false, message:'PIN incorrect', pin:pin});
        }
      }).catch(err => {
        document.getElementById('loginStatus').innerText='Erreur: '+err;
        appendDebug(`login error => ${err}`);
        sendLoginEvent('login_result', {success:false, message:'Erreur: '+err, pin:pin});
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
      fetch('/api/io', {credentials:'same-origin'})
        .then(checkAuthResponse)
        .then(r => r.json()).then(data => {
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
      fetch('/api/dmm', {credentials:'same-origin'})
        .then(checkAuthResponse)
        .then(r => r.json()).then(data => {
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
      fetch('/api/config/funcgen', {credentials:'same-origin'})
        .then(checkAuthResponse)
        .then(r => r.json()).then(cfg => {
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
        credentials:'same-origin',
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

String extractDigits(const String& value) {
  String digits;
  digits.reserve(4);
  for (size_t i = 0; i < value.length() && digits.length() < 4; ++i) {
    char c = value.charAt(i);
    if (c >= '0' && c <= '9') {
      digits += c;
    }
  }
  return digits;
}

String normalizePin(const String& value) {
  String digits = extractDigits(value);
  if (digits.length() == 0) {
    digits = F("0000");
  }
  while (digits.length() < 4) {
    digits = String('0') + digits;
  }
  return digits;
}

bool handleLoginEventPayload(JsonVariantConst payload,
                             String& normalizedType,
                             String& errorMessage) {
  normalizedType = String();
  errorMessage = String();
  if (payload.isNull() || !payload.is<JsonObject>()) {
    errorMessage = F("Invalid JSON");
    return false;
  }

  JsonVariantConst typeVariant = payload["type"];
  if (typeVariant.isNull()) {
    errorMessage = F("Missing type");
    return false;
  }

  String type = typeVariant.as<String>();
  type.trim();
  type.toLowerCase();
  if (!type.length()) {
    errorMessage = F("Missing type");
    return false;
  }
  normalizedType = type;

  if (type == F("page_load")) {
    OledPin::pushErrorMessage(F("Client login connecté"));
    OledPin::setSubmittedPin(String());
    OledPin::setTestStatus(F("---"));
    return true;
  }

  if (type == F("pin_update")) {
    OledPin::setSubmittedPin(payload["pin"].as<String>());
    return true;
  }

  if (type == F("login_result")) {
    bool success = payload["success"].as<bool>();
    String message = payload["message"].as<String>();
    if (!message.length()) {
      message = success ? F("Connexion OK") : F("PIN incorrect");
    }
    OledPin::setSubmittedPin(payload["pin"].as<String>());
    OledPin::setTestStatus(success ? F("OK") : message);
    OledPin::pushErrorMessage(message);
    return true;
  }

  if (type == F("test_message")) {
    String message = payload["message"].as<String>();
    message.trim();
    if (!message.length()) {
      message = F("test");
    }
    OledPin::setTestStatus(message);
    OledPin::pushErrorMessage(String(F("Test OLED: ")) + message);
    return true;
  }

  errorMessage = F("Unknown type");
  return false;
}

}  // namespace

void WebServer::setExpectedPin(int pin) {
  setExpectedPin(String(pin));
}

void WebServer::setExpectedPin(const String& pin) {
  _expectedPin = normalizePin(pin);
  _hasAuthenticatedClient = false;
  OledPin::setExpectedPin(_expectedPin);
}

String WebServer::expectedPin() {
  return _expectedPin;
}

namespace {

String readConfiguredPin() {
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
  return normalizePin(expectedRaw);
}

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
AsyncWebSocket WebServer::_wsUi("/ws/ui");
int WebServer::_logClients = 0;
int WebServer::_uiClients = 0;
bool WebServer::_started = false;
bool WebServer::_hasAuthenticatedClient = false;
String WebServer::_expectedPin;

bool WebServer::begin() {
  ensureIndexHtmlPresent();
  _started = false;
  _hasAuthenticatedClient = false;
  if (_expectedPin.length() != 4) {
    String configured = readConfiguredPin();
    WebServer::setExpectedPin(configured);
  }
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

  _wsUi.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client,
                   AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
      _uiClients++;
      Logger::info("WS", "ui_connect", String("Client UI connected: ") + _uiClients);
      StaticJsonDocument<96> hello;
      hello["ok"] = true;
      hello["type"] = F("hello");
      hello["clients"] = _uiClients;
      String out;
      serializeJson(hello, out);
      client->text(out);
      return;
    }

    if (type == WS_EVT_DISCONNECT) {
      _uiClients--;
      if (_uiClients < 0) _uiClients = 0;
      Logger::info("WS", "ui_disconnect", String("Client UI disconnected: ") + _uiClients);
      return;
    }

    if (type != WS_EVT_DATA) {
      return;
    }

    AwsFrameInfo *info = reinterpret_cast<AwsFrameInfo *>(arg);
    if (!info || info->opcode != WS_TEXT) {
      return;
    }
    if (!info->final || info->index != 0) {
      Logger::warn("WS", "ui_event", "Ignoring fragmented UI frame");
      return;
    }

    String payload;
    payload.reserve(info->len + 1);
    for (size_t i = 0; i < len; ++i) {
      payload += static_cast<char>(data[i]);
    }

    size_t docCapacity = payload.length() + 128;
    if (docCapacity < 256) {
      docCapacity = 256;
    }
    DynamicJsonDocument doc(docCapacity);
    StaticJsonDocument<192> response;
    String normalizedType;
    String error;

    DeserializationError err = deserializeJson(doc, payload.c_str(), payload.length());
    if (err) {
      String truncated = payload;
      if (truncated.length() > 96) {
        truncated = truncated.substring(0, 96) + F("...");
      }
      Logger::warn("WS", "ui_event",
                   String("Invalid JSON: ") + err.c_str() + F(" payload=") + truncated);
      response["ok"] = false;
      response["error"] = F("invalid_json");
      response["details"] = err.c_str();
    } else if (!doc.is<JsonObject>()) {
      Logger::warn("WS", "ui_event",
                   String("Invalid JSON root type payload=") + payload);
      response["ok"] = false;
      response["error"] = F("invalid_json");
      response["details"] = F("root_not_object");
    } else if (handleLoginEventPayload(doc.as<JsonVariantConst>(), normalizedType, error)) {
      response["ok"] = true;
      response["type"] = normalizedType;
      response["transport"] = F("ws");
    } else {
      response["ok"] = false;
      response["error"] = error;
      if (normalizedType.length()) {
        response["type"] = normalizedType;
      }
    }

    String out;
    serializeJson(response, out);
    client->text(out);
  });
  _server.addHandler(&_wsUi);

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
  _server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // Route POST /login
  _server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Lire le corps JSON
    String body = readRequestBody(request);
    if (body.length()) {
      StaticJsonDocument<128> doc;
      DeserializationError err = deserializeJson(doc, body);
      if (err) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
        return;
      }

      String submittedSanitized = extractDigits(doc["pin"].as<String>());
      OledPin::setSubmittedPin(submittedSanitized);
      if (submittedSanitized.length() != 4) {
        OledPin::setTestStatus(F("PIN incomplet"));
        OledPin::pushErrorMessage(F("PIN incorrect"));
        request->send(401, "application/json", "{\"success\":false,\"error\":\"PIN incorrect\"}");
        return;
      }
      String expectedSanitized = WebServer::_expectedPin;
      if (expectedSanitized.length() != 4) {
        expectedSanitized = readConfiguredPin();
        WebServer::setExpectedPin(expectedSanitized);
        expectedSanitized = WebServer::_expectedPin;
      }

      if (expectedSanitized.length() == 4) {
        OledPin::pushErrorMessage(String(F("PIN cfg=")) + expectedSanitized +
                                  F(" login=") + submittedSanitized);
      }

      if (expectedSanitized.length() == 4 &&
          submittedSanitized == expectedSanitized) {
        OledPin::setTestStatus(F("OK"));
        // Auth ok : définir cookie
        _hasAuthenticatedClient = true;
        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"success\":true}");
        response->addHeader("Set-Cookie", String("mlpin=1; Path=/"));
        request->send(response);
        return;
      }
    }
    OledPin::setTestStatus(F("PIN incorrect"));
    OledPin::pushErrorMessage(F("PIN incorrect"));
    request->send(401, "application/json", "{\"success\":false,\"error\":\"PIN incorrect\"}");
  });

  _server.on("/api/login/event", HTTP_POST, [](AsyncWebServerRequest *request) {
    String body = readRequestBody(request);
    if (!body.length()) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing body\"}");
      return;
    }
    size_t docCapacity = body.length() + 128;
    if (docCapacity < 256) {
      docCapacity = 256;
    }
    DynamicJsonDocument doc(docCapacity);
    DeserializationError err = deserializeJson(doc, body.c_str(), body.length());
    if (err) {
      StaticJsonDocument<128> resp;
      resp["ok"] = false;
      resp["error"] = F("Invalid JSON");
      resp["details"] = err.c_str();
      String out;
      serializeJson(resp, out);
      request->send(400, "application/json", out);
      return;
    }

    String normalizedType;
    String error;
    bool handled = false;
    if (doc.is<JsonObject>()) {
      handled = handleLoginEventPayload(doc.as<JsonVariantConst>(), normalizedType, error);
    } else {
      error = F("Invalid JSON");
    }
    if (!handled) {
      StaticJsonDocument<128> resp;
      resp["ok"] = false;
      if (error.length()) {
        resp["error"] = error;
      } else {
        resp["error"] = F("Unknown error");
      }
      if (normalizedType.length()) {
        resp["type"] = normalizedType;
      }
      String out;
      serializeJson(resp, out);
      request->send(400, "application/json", out);
      return;
    }

    StaticJsonDocument<128> resp;
    resp["ok"] = true;
    resp["type"] = normalizedType;
    resp["transport"] = F("http");
    String out;
    serializeJson(resp, out);
    request->send(200, "application/json", out);
  });

  // Route GET /api/io
  _server.on("/api/io", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!checkAuth(request)) {
      request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
      return;
    }
    StaticJsonDocument<1024> doc;
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
    StaticJsonDocument<256> doc;
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
    StaticJsonDocument<1024> doc;
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
    StaticJsonDocument<256> doc;
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
    StaticJsonDocument<256> doc;
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
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, body)) {
      request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    JsonDocument &cfg = ConfigStore::doc(area);
    cfg.clear();
    cfg = doc;
    ConfigStore::requestSave(area);
    if (area == "general") {
      WebServer::setExpectedPin(cfg["pin"].as<String>());
    } else if (area == "io") {
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
  if (!request) {
    return String();
  }

  String result;
  if (request->_tempObject) {
    String *body = static_cast<String *>(request->_tempObject);
    if (body) {
      result = *body;
      delete body;
    }
    request->_tempObject = nullptr;
  }

  if (result.length() > 0) {
    return result;
  }

  if (request->hasParam(F("plain"), true)) {
    const AsyncWebParameter *param = request->getParam(F("plain"), true);
    if (param) {
      result = param->value();
      if (result.length() > 0) {
        return result;
      }
    }
  }

  if (request->hasParam(F("plain"))) {
    const AsyncWebParameter *param = request->getParam(F("plain"));
    if (param) {
      result = param->value();
    }
  }

  return result;
}

void WebServer::logCallback(const String& line) {
  // Diffuse la ligne sur les clients WebSocket actifs
  if (_logClients > 0) {
    _wsLogs.textAll(line);
  }
}

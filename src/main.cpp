#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

#include <vector>

extern "C" {
#include <user_interface.h>
}

#include "core/ConfigStore.h"
#include "core/Logger.h"
#include "core/IORegistry.h"

#include "ui/WebServer.h"
#include "OledPin.h"

#include "devices/DMM.h"
#include "devices/Scope.h"
#include "devices/FuncGen.h"
#include "network/UDPServer.h"

namespace {
constexpr char kAccessPointSsidPrefix[] = "MiniLabo";
const IPAddress kAccessPointIp(192, 168, 4, 1);
const IPAddress kAccessPointGateway(192, 168, 4, 1);
const IPAddress kAccessPointSubnet(255, 255, 255, 0);

int generateSessionPin() {
  uint32_t value = os_random();
  return static_cast<int>(value % 9000U) + 1000;
}

struct SystemStatus {
  String wifiLine{"WiFi: INIT"};
  String wifiHardware;
  String webLine{"Web: OFF"};
  String udpLine{"UDP: OFF"};
};

SystemStatus g_status;
enum class OledDisplayMode {
  STATUS,
  IO
};
OledDisplayMode g_oledMode = OledDisplayMode::STATUS;
bool g_webAvailable = false;
uint16_t g_webPort = 0;
bool g_udpEnabled = false;
bool g_udpRunning = false;
uint16_t g_udpPort = 0;
unsigned long g_lastStatusRefresh = 0;
bool g_oledInitialised = false;
std::vector<String> g_deferredOledMessages;
unsigned long g_lastPeripheralTick = 0;
unsigned long g_lastLoggerTick = 0;
WiFiEventHandler g_apStationConnectedHandler;
WiFiEventHandler g_apStationDisconnectedHandler;
String g_accessPointSsid;
String g_accessPointPassword;
int g_accessPointChannel = 1;
bool g_accessPointHidden = false;
int g_sessionPin = 0;
unsigned long g_lastIoDisplay = 0;

constexpr unsigned long kPeripheralIntervalMs = 5;
constexpr unsigned long kLoggerIntervalMs = 20;
constexpr unsigned long kOledIoRefreshMs = 1000;

String formatMacCompact(const uint8_t* mac) {
  char buf[13];
  snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

String phyModeToString(WiFiPhyMode_t mode) {
  switch (mode) {
    case WIFI_PHY_MODE_11B: return F("11B");
    case WIFI_PHY_MODE_11G: return F("11G");
    case WIFI_PHY_MODE_11N: return F("11N");
    default:                return F("UNK");
  }
}

String computeWifiHardware() {
  uint8_t mac[6];
  WiFi.softAPmacAddress(mac);
  String macStr = String("M") + formatMacCompact(mac);

  int channel = WiFi.channel();
  if (channel <= 0) {
    channel = 1;
  }

  String phy = phyModeToString(WiFi.getPhyMode());
  return macStr + String(" C") + channel + String("/") + phy;
}

String computeDefaultAccessPointSsid() {
  uint8_t mac[6];
  WiFi.softAPmacAddress(mac);
  String macCompact = formatMacCompact(mac);
  String suffix = macCompact.substring(8);
  return String(kAccessPointSsidPrefix) + "-" + suffix;
}

String trimmedString(const char* value) {
  if (!value) {
    return String();
  }
  String result(value);
  result.trim();
  return result;
}

void enqueueOledMessage(const String& message);

void loadAccessPointConfig() {
  g_accessPointSsid = String();
  g_accessPointPassword = String();
  g_accessPointChannel = 1;
  g_accessPointHidden = false;

  JsonDocument& netDoc = ConfigStore::doc("network");
  JsonVariant apVariant = netDoc["ap"];
  if (!apVariant.isNull()) {
    const char* cfgSsid = apVariant["ssid"] | "";
    const char* cfgPassword = apVariant["password"] | "";
    int cfgChannel = apVariant["channel"] | 1;
    bool cfgHidden = apVariant["hidden"] | false;

    g_accessPointSsid = trimmedString(cfgSsid);
    g_accessPointPassword = trimmedString(cfgPassword);
    g_accessPointChannel = constrain(cfgChannel, 1, 13);
    g_accessPointHidden = cfgHidden;
  }

  if (g_accessPointSsid.isEmpty()) {
    g_accessPointSsid = computeDefaultAccessPointSsid();
  }
}

const char* selectAccessPointPassword(bool verbose, bool logWarning) {
  if (g_accessPointPassword.length() == 0) {
    return nullptr;
  }
  if (g_accessPointPassword.length() < 8) {
    if (verbose) {
      Serial.println(F("[MiniLabo] Mot de passe AP trop court (<8), AP ouvert"));
    }
    if (logWarning) {
      Logger::warn("NET", "AP", "AP password too short, using open AP");
    }
    enqueueOledMessage(F("AP pass <8, ouvert"));
    return nullptr;
  }
  return g_accessPointPassword.c_str();
}

void enqueueOledMessage(const String& message) {
  String trimmed = message;
  trimmed.trim();
  if (trimmed.length() == 0) {
    return;
  }

  if (g_oledInitialised) {
    OledPin::pushErrorMessage(trimmed);
  } else {
    g_deferredOledMessages.push_back(trimmed);
  }
}

void flushDeferredOledMessages() {
  if (!g_oledInitialised) {
    return;
  }
  for (const auto& msg : g_deferredOledMessages) {
    OledPin::pushErrorMessage(msg);
  }
  g_deferredOledMessages.clear();
}

void refreshIoDisplay() {
  if (!g_oledInitialised) {
    return;
  }
  auto ios = IORegistry::list();
  OledPin::showIOValues(ios);
}

void updateServiceStatus() {
  if (g_webAvailable) {
    g_status.webLine = String(F("Web: ON :")) + g_webPort;
  } else {
    g_status.webLine = F("Web: ERROR");
  }

  if (!g_udpEnabled) {
    g_status.udpLine = F("UDP: OFF");
  } else if (g_udpRunning) {
    g_status.udpLine = String(F("UDP: ON :")) + g_udpPort;
  } else {
    g_status.udpLine = F("UDP: ERROR");
  }
}

void updateStatusDisplay(bool force) {
  if (!g_oledInitialised) {
    return;
  }

  if (g_oledMode != OledDisplayMode::STATUS) {
    return;
  }

  unsigned long now = millis();
  if (!force && now - g_lastStatusRefresh < 1000) {
    return;
  }
  g_lastStatusRefresh = now;

  String wifiLine = g_status.wifiLine + String(" (") + WiFi.softAPgetStationNum() + String(")");

  static String prevWifi;
  static String prevWifiHardware;
  static String prevWeb;
  static String prevUdp;

  if (force || wifiLine != prevWifi ||
      g_status.wifiHardware != prevWifiHardware ||
      g_status.webLine != prevWeb ||
      g_status.udpLine != prevUdp) {
    OledPin::showStatus(wifiLine, g_status.wifiHardware, g_status.webLine, g_status.udpLine);
    prevWifi = wifiLine;
    prevWifiHardware = g_status.wifiHardware;
    prevWeb = g_status.webLine;
    prevUdp = g_status.udpLine;
  }
}

void handleLogLineForDisplay(const String& line) {
  bool isError = line.indexOf("[E]") >= 0;
  bool isWarning = line.indexOf("[W]") >= 0;
  if (!isError && !isWarning) {
    return;
  }

  int prefixEnd = line.indexOf("] ");
  if (prefixEnd < 0 || prefixEnd + 2 >= line.length()) {
    return;
  }

  String payload = line.substring(prefixEnd + 2);
  enqueueOledMessage(payload);
}

bool startAccessPointVerbose() {
  Serial.println();
  Serial.println(F("[MiniLabo] Initialisation en mode point d'accès"));

  WiFi.persistent(false);
  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_AP);

  if (g_accessPointSsid.isEmpty()) {
    g_accessPointSsid = computeDefaultAccessPointSsid();
  }

  const char* password = selectAccessPointPassword(true, false);
  int channel = constrain(g_accessPointChannel, 1, 13);
  bool hidden = g_accessPointHidden;

  bool apConfigured = WiFi.softAPConfig(kAccessPointIp, kAccessPointGateway, kAccessPointSubnet);
  if (!apConfigured) {
    Serial.println(F("[MiniLabo] Échec de la configuration IP de l'AP"));
    Logger::warn("NET", "AP", "softAPConfig failed (pre-start)");
    enqueueOledMessage(F("softAP config failed"));
  }

  if (!WiFi.softAP(g_accessPointSsid.c_str(), password, channel, hidden)) {
    Serial.println(F("[MiniLabo] Impossible de démarrer le point d'accès"));
    g_status.wifiLine = F("WiFi: ERROR");
    g_status.wifiHardware = F("AP init failed");
    enqueueOledMessage(F("softAP start failed"));
    return false;
  }

  if (!apConfigured) {
    if (!WiFi.softAPConfig(kAccessPointIp, kAccessPointGateway, kAccessPointSubnet)) {
      Logger::warn("NET", "AP", "softAPConfig failed");
      enqueueOledMessage(F("softAP config failed"));
    }
  }

  Serial.print(F("[MiniLabo] Point d'accès démarré : "));
  Serial.println(g_accessPointSsid);
  Serial.print(F("[MiniLabo] Adresse IP : "));
  Serial.println(WiFi.softAPIP());
  g_status.wifiLine = String(F("WiFi: AP ")) + g_accessPointSsid;
  g_status.wifiHardware = computeWifiHardware();
  enqueueOledMessage(String(F("AP prêt: ")) + g_accessPointSsid);
  return true;
}

bool startAccessPointSilent() {
  WiFi.persistent(false);
  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_AP);
  if (g_accessPointSsid.isEmpty()) {
    g_accessPointSsid = computeDefaultAccessPointSsid();
  }

  const char* password = selectAccessPointPassword(false, true);
  int channel = constrain(g_accessPointChannel, 1, 13);
  bool hidden = g_accessPointHidden;

  bool apConfigured = WiFi.softAPConfig(kAccessPointIp, kAccessPointGateway, kAccessPointSubnet);
  if (!apConfigured) {
    Logger::warn("NET", "AP", "softAPConfig failed (pre-start)");
    enqueueOledMessage(F("softAP config failed"));
  }

  if (!WiFi.softAP(g_accessPointSsid.c_str(), password, channel, hidden)) {
    g_status.wifiLine = F("WiFi: ERROR");
    g_status.wifiHardware = F("AP init failed");
    enqueueOledMessage(F("softAP restart failed"));
    return false;
  }

  if (!apConfigured) {
    if (!WiFi.softAPConfig(kAccessPointIp, kAccessPointGateway, kAccessPointSubnet)) {
      Logger::warn("NET", "AP", "softAPConfig failed");
      enqueueOledMessage(F("softAP config failed"));
    }
  }

  g_status.wifiLine = String(F("WiFi: AP ")) + g_accessPointSsid;
  g_status.wifiHardware = computeWifiHardware();
  return true;
}

void maintainAccessPoint() {
  if (WiFi.getMode() != WIFI_AP) {
    loadAccessPointConfig();
    if (!startAccessPointSilent()) {
      Logger::error("NET", "maintain", "Failed to restart AP");
    }
  }
  if (WiFi.getMode() == WIFI_AP) {
    if (g_accessPointSsid.isEmpty()) {
      g_accessPointSsid = computeDefaultAccessPointSsid();
    }
    g_status.wifiLine = String(F("WiFi: AP ")) + g_accessPointSsid;
    g_status.wifiHardware = computeWifiHardware();
  }
}

void setupAccessPointEventHandlers() {
  g_apStationConnectedHandler = WiFi.onSoftAPModeStationConnected([](const WiFiEventSoftAPModeStationConnected& event) {
    String mac = formatMacCompact(event.mac);
    Logger::info("NET", "AP", String("Station connected ") + mac);
    enqueueOledMessage(String(F("Client +")) + mac);
    updateStatusDisplay(true);
  });

  g_apStationDisconnectedHandler = WiFi.onSoftAPModeStationDisconnected([](const WiFiEventSoftAPModeStationDisconnected& event) {
    String mac = formatMacCompact(event.mac);
    Logger::warn("NET", "AP", String("Station disconnected ") + mac);
    enqueueOledMessage(String(F("Client -")) + mac);
    updateStatusDisplay(true);
  });
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println();
  Serial.println(F("[MiniLabo] Booting..."));

  if (!LittleFS.begin()) {
    Serial.println(F("[MiniLabo] Failed to mount LittleFS, formatting..."));
    if (!LittleFS.format()) {
      Serial.println(F("[MiniLabo] LittleFS format failed"));
      return;
    }
    if (!LittleFS.begin()) {
      Serial.println(F("[MiniLabo] LittleFS mount failed after format"));
      return;
    }
  }
  Serial.println(F("[MiniLabo] LittleFS ready"));

  ConfigStore::begin();

  auto& generalDoc = ConfigStore::doc("general");
  g_sessionPin = generateSessionPin();
  generalDoc["pin"] = g_sessionPin;
  ConfigStore::requestSave("general");
  WebServer::setExpectedPin(g_sessionPin);

  loadAccessPointConfig();

  bool apStarted = startAccessPointVerbose();
  if (!apStarted) {
    Logger::error("NET", "setup", "Failed to start AP");
  }

  setupAccessPointEventHandlers();

  OledPin::begin();
  g_oledInitialised = true;
  flushDeferredOledMessages();
  OledPin::setSessionPin(g_sessionPin);
  OledPin::setExpectedPin(WebServer::expectedPin());

  Logger::begin();
  Logger::setLogCallback(handleLogLineForDisplay);

  IORegistry::begin();

  bool webStarted = WebServer::begin();
  g_webAvailable = webStarted && WebServer::isStarted();
  g_webPort = WebServer::port();

  g_udpRunning = UDPServer::begin();
  g_udpEnabled = UDPServer::isEnabled();
  g_udpPort = UDPServer::port();

  updateServiceStatus();
  updateStatusDisplay(true);

  Logger::info("SYS", "setup", "System initialised");
}

void loop() {
  unsigned long now = millis();

  maintainAccessPoint();
  WebServer::loop();
  UDPServer::loop();

  if (now - g_lastLoggerTick >= kLoggerIntervalMs) {
    Logger::loop();
    g_lastLoggerTick = now;
  }

  if (now - g_lastPeripheralTick >= kPeripheralIntervalMs) {
    ConfigStore::loop();
    IORegistry::loop();
    DMM::loop();
    Scope::loop();
    FuncGen::loop();
    g_lastPeripheralTick = now;
  }

  updateServiceStatus();
  bool hasClient = WebServer::hasAuthenticatedClient();
  OledDisplayMode desiredMode = hasClient ? OledDisplayMode::IO : OledDisplayMode::STATUS;
  if (desiredMode != g_oledMode) {
    g_oledMode = desiredMode;
    if (g_oledMode == OledDisplayMode::STATUS) {
      g_lastIoDisplay = 0;
      updateStatusDisplay(true);
    } else {
      g_lastIoDisplay = 0;
    }
  }

  if (g_oledMode == OledDisplayMode::STATUS) {
    updateStatusDisplay(false);
  } else {
    if (g_lastIoDisplay == 0 || now - g_lastIoDisplay >= kOledIoRefreshMs) {
      refreshIoDisplay();
      g_lastIoDisplay = now;
    }
  }
  OledPin::loop();

  yield();
}


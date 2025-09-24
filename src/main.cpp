#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

#include <vector>

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
constexpr char kAccessPointSsid[] = "MiniLabo";
const IPAddress kAccessPointIp(192, 168, 4, 1);
const IPAddress kAccessPointGateway(192, 168, 4, 1);
const IPAddress kAccessPointSubnet(255, 255, 255, 0);

struct SystemStatus {
  String wifiLine{"WiFi: INIT"};
  String wifiHardware;
  String webLine{"Web: OFF"};
  String udpLine{"UDP: OFF"};
};

SystemStatus g_status;
bool g_webAvailable = false;
uint16_t g_webPort = 0;
bool g_udpEnabled = false;
bool g_udpRunning = false;
uint16_t g_udpPort = 0;
unsigned long g_lastStatusRefresh = 0;
bool g_oledInitialised = false;
std::vector<String> g_deferredOledMessages;

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

  if (!WiFi.softAPConfig(kAccessPointIp, kAccessPointGateway, kAccessPointSubnet)) {
    Serial.println(F("[MiniLabo] Échec de la configuration IP de l'AP"));
    Logger::warn("NET", "AP", "softAPConfig failed");
  }

  WiFi.mode(WIFI_AP);

  if (WiFi.softAP(kAccessPointSsid)) {
    Serial.print(F("[MiniLabo] Point d'accès démarré : "));
    Serial.println(kAccessPointSsid);
    Serial.print(F("[MiniLabo] Adresse IP : "));
    Serial.println(WiFi.softAPIP());
    g_status.wifiLine = String(F("WiFi: AP ")) + kAccessPointSsid;
    g_status.wifiHardware = computeWifiHardware();
    return true;
  } else {
    Serial.println(F("[MiniLabo] Impossible de démarrer le point d'accès"));
    g_status.wifiLine = F("WiFi: ERROR");
    g_status.wifiHardware = F("AP init failed");
    enqueueOledMessage(F("softAP start failed"));
    return false;
  }
}

bool startAccessPointSilent() {
  WiFi.persistent(false);
  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);

  if (!WiFi.softAPConfig(kAccessPointIp, kAccessPointGateway, kAccessPointSubnet)) {
    Logger::warn("NET", "AP", "softAPConfig failed");
  }

  WiFi.mode(WIFI_AP);
  if (WiFi.softAP(kAccessPointSsid)) {
    g_status.wifiLine = String(F("WiFi: AP ")) + kAccessPointSsid;
    g_status.wifiHardware = computeWifiHardware();
    return true;
  }

  g_status.wifiLine = F("WiFi: ERROR");
  g_status.wifiHardware = F("AP init failed");
  enqueueOledMessage(F("softAP restart failed"));
  return false;
}

void maintainAccessPoint() {
  if (WiFi.getMode() != WIFI_AP) {
    if (!startAccessPointSilent()) {
      Logger::error("NET", "maintain", "Failed to restart AP");
    }
  }
  if (WiFi.getMode() == WIFI_AP) {
    g_status.wifiLine = String(F("WiFi: AP ")) + kAccessPointSsid;
    g_status.wifiHardware = computeWifiHardware();
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println();
  Serial.println(F("[MiniLabo] Booting..."));

  if (!LittleFS.begin()) {
    Serial.println(F("[MiniLabo] Failed to mount LittleFS"));
    return;
  }
  Serial.println(F("[MiniLabo] LittleFS mounted"));

  ConfigStore::begin();

  bool apStarted = startAccessPointVerbose();
  if (!apStarted) {
    Logger::error("NET", "setup", "Failed to start AP");
  }

  OledPin::begin();
  g_oledInitialised = true;
  flushDeferredOledMessages();

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
  ConfigStore::loop();
  Logger::loop();
  IORegistry::loop();
  DMM::loop();
  Scope::loop();
  FuncGen::loop();
  WebServer::loop();
  UDPServer::loop();

  maintainAccessPoint();
  updateServiceStatus();
  updateStatusDisplay(false);
  OledPin::loop();

  yield();
}


#include <Arduino.h>
#include <ESP8266WiFi.h>

namespace {
constexpr char kAccessPointSsid[] = "MiniLabo";
const IPAddress kAccessPointIp(192, 168, 4, 1);
const IPAddress kAccessPointGateway(192, 168, 4, 1);
const IPAddress kAccessPointSubnet(255, 255, 255, 0);
}

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println();
  Serial.println(F("[MiniLabo] Initialisation en mode point d'accès"));

  WiFi.persistent(false);
  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);

  if (!WiFi.softAPConfig(kAccessPointIp, kAccessPointGateway, kAccessPointSubnet)) {
    Serial.println(F("[MiniLabo] Échec de la configuration IP de l'AP"));
  }

  WiFi.mode(WIFI_AP);

  if (WiFi.softAP(kAccessPointSsid)) {
    Serial.print(F("[MiniLabo] Point d'accès démarré : "));
    Serial.println(kAccessPointSsid);
    Serial.print(F("[MiniLabo] Adresse IP : "));
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println(F("[MiniLabo] Impossible de démarrer le point d'accès"));
  }
}

void loop() {
  delay(1000);
}

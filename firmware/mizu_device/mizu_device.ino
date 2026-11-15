/*
  Mizu tester firmware (ESP32)
  ---------------------------------
  Sends pH and total alkalinity to the Mizu app two ways:
    1. Bluetooth Low Energy  - nearby (about 10-100 m, depends on antenna and walls)
    2. Wi-Fi + MQTT          - from anywhere with internet (the "long distance" mode)

  Board:     ESP32 DevKit (best range: ESP32-WROOM-32U with an external 2.4 GHz antenna)
  Libraries: NimBLE-Arduino (h2zero, v1.4.x), PubSubClient (Nick O'Leary)
             Install both from Arduino IDE > Library Manager.

  Wiring:
    pH probe board (e.g. DFRobot Gravity Analog pH V2) signal -> GPIO 34
    Alkalinity analyzer with serial output (TX)                -> GPIO 16 (RX2)
    Optional DS18B20 temperature is not included to keep this short.

  Bluetooth protocol (must match index.html):
    Service   6e7a0001-3c1f-4b8e-9a4e-c1ea7e110001
    Reading   6e7a0002-...  READ + NOTIFY  JSON {"id":"MZ-1A2B3C","ph":7.42,"alk":104,"t":25.0}
    Command   6e7a0003-...  WRITE          "MEASURE"  or  "WIFI:<ssid>\n<password>"

  MQTT topics:
    mizu/<id>/reading  (retained JSON, same as above)
    mizu/<id>/cmd      ("MEASURE")
*/

#include <NimBLEDevice.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>

// ---------- Settings ----------
const char* MQTT_HOST = "broker.hivemq.com";  // public test server; use your own for privacy
const int   MQTT_PORT = 1883;
const uint32_t SEND_EVERY_MS = 5000;           // Bluetooth update rate
const uint32_t PUBLISH_EVERY_MS = 60000;       // internet update rate

const int PH_PIN = 34;
// Two-point calibration: measure probe voltage in pH 7.00 and pH 4.00 buffer and enter here.
float PH7_VOLTAGE = 1.500;
float PH4_VOLTAGE = 2.032;

#define SERVICE_UUID  "6e7a0001-3c1f-4b8e-9a4e-c1ea7e110001"
#define READING_UUID  "6e7a0002-3c1f-4b8e-9a4e-c1ea7e110001"
#define COMMAND_UUID  "6e7a0003-3c1f-4b8e-9a4e-c1ea7e110001"

// ---------- State ----------
Preferences prefs;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
NimBLECharacteristic* readingChar;
String deviceId, wifiSsid, wifiPass;
float lastPh = 7.0, lastAlk = 0, lastTemp = 25.0;
volatile bool measureNow = true, wifiChanged = false;
uint32_t lastSend = 0, lastPublish = 0, lastWifiTry = 0;

// ---------- Sensors ----------
float readPh() {
  long sum = 0;
  for (int i = 0; i < 40; i++) { sum += analogReadMilliVolts(PH_PIN); delay(5); }
  float v = sum / 40.0 / 1000.0;
  float slope = (PH4_VOLTAGE - PH7_VOLTAGE) / 3.0;  // volts per pH unit
  return 7.0 - (v - PH7_VOLTAGE) / slope;
}

// Total alkalinity can't be read by a simple probe; it needs a titration-based
// analyzer. This reads lines like "TA:104" (ppm as CaCO3) from such an analyzer
// on Serial2. Adapt it to your analyzer's output format.
float readAlkalinity() {
  while (Serial2.available()) {
    String line = Serial2.readStringUntil('\n');
    line.trim();
    if (line.startsWith("TA:")) lastAlk = line.substring(3).toFloat();
  }
  return lastAlk;
}

String readingJson() {
  char buf[96];
  snprintf(buf, sizeof(buf), "{\"id\":\"%s\",\"ph\":%.2f,\"alk\":%.0f,\"t\":%.1f}",
           deviceId.c_str(), lastPh, lastAlk, lastTemp);
  return String(buf);
}

// ---------- Bluetooth ----------
class CommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    String cmd = String(c->getValue().c_str());
    if (cmd == "MEASURE") measureNow = true;
    else if (cmd.startsWith("WIFI:")) {
      int nl = cmd.indexOf('\n');
      wifiSsid = cmd.substring(5, nl < 0 ? cmd.length() : nl);
      wifiPass = nl < 0 ? "" : cmd.substring(nl + 1);
      prefs.putString("ssid", wifiSsid);
      prefs.putString("pass", wifiPass);
      wifiChanged = true;
    }
  }
};

class ServerCallbacks : public NimBLEServerCallbacks {
  void onDisconnect(NimBLEServer*) override { NimBLEDevice::startAdvertising(); }
};

void setupBle() {
  NimBLEDevice::init(("Mizu " + deviceId.substring(3)).c_str());
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // maximum transmit power for best range
  NimBLEDevice::setMTU(185);               // readings (~60 bytes) fit in one notification
  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  NimBLEService* svc = server->createService(SERVICE_UUID);
  readingChar = svc->createCharacteristic(READING_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* cmd = svc->createCharacteristic(COMMAND_UUID, NIMBLE_PROPERTY::WRITE);
  cmd->setCallbacks(new CommandCallbacks());
  svc->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();
}

// ---------- Wi-Fi + MQTT (long distance) ----------
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  String msg; for (unsigned i = 0; i < len; i++) msg += (char)payload[i];
  if (msg == "MEASURE") measureNow = true;
}

void maintainInternet() {
  if (wifiSsid.isEmpty()) return;
  if (wifiChanged) { wifiChanged = false; WiFi.disconnect(); WiFi.begin(wifiSsid.c_str(), wifiPass.c_str()); lastWifiTry = millis(); }
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastWifiTry > 30000) { WiFi.begin(wifiSsid.c_str(), wifiPass.c_str()); lastWifiTry = millis(); }
    return;
  }
  if (!mqtt.connected()) {
    String clientId = "mizu-dev-" + deviceId;
    if (mqtt.connect(clientId.c_str())) {
      mqtt.subscribe(("mizu/" + deviceId + "/cmd").c_str());
      lastPublish = 0;  // publish right away after reconnecting
    } else return;
  }
  mqtt.loop();
}

void publishReading() {
  if (!mqtt.connected()) return;
  String topic = "mizu/" + deviceId + "/reading";
  mqtt.publish(topic.c_str(), readingJson().c_str(), true);  // retained: app gets the last reading at once
}

// ---------- Main ----------
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, 16, 17);
  analogSetPinAttenuation(PH_PIN, ADC_11db);

  WiFi.mode(WIFI_STA);  // must come before reading the MAC address
  uint8_t mac[6]; WiFi.macAddress(mac);
  char id[10]; snprintf(id, sizeof(id), "MZ-%02X%02X%02X", mac[3], mac[4], mac[5]);
  deviceId = id;
  Serial.printf("Mizu tester ID: %s\n", id);

  prefs.begin("mizu", false);
  wifiSsid = prefs.getString("ssid", "");
  wifiPass = prefs.getString("pass", "");

  WiFi.setAutoReconnect(true);
  if (!wifiSsid.isEmpty()) { WiFi.begin(wifiSsid.c_str(), wifiPass.c_str()); lastWifiTry = millis(); }
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);

  setupBle();
}

void loop() {
  maintainInternet();

  bool forced = measureNow;
  if (forced || millis() - lastSend > SEND_EVERY_MS) {
    measureNow = false;
    lastSend = millis();
    lastPh = readPh();
    lastAlk = readAlkalinity();
    String json = readingJson();
    readingChar->setValue(json.c_str());
    readingChar->notify();
    Serial.println(json);
    if (forced || millis() - lastPublish > PUBLISH_EVERY_MS) { lastPublish = millis(); publishReading(); }
  }
  delay(20);
}

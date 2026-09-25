#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

// ---------------------------------------------------------------------------
// Limits
// ---------------------------------------------------------------------------
#define MAX_RELAYS 8
#define CONFIG_PATH "/config.json"

// Arduino digital pins on the ATmega328P side that are safe to wire a relay
// to on this hybrid board. D0/D1 are reserved for the CH340 USB-serial link
// and D10/D11 are reserved for the SoftwareSerial link to the ESP8266 (see
// README.md). Keep this list in sync with RelayController.ino ALLOWED_PINS.
const uint8_t ALLOWED_PINS[] = {2, 3, 4, 5, 6, 7, 8, 9, 12, 13};
const uint8_t ALLOWED_PINS_COUNT = sizeof(ALLOWED_PINS) / sizeof(ALLOWED_PINS[0]);

inline bool isAllowedPin(uint8_t pin) {
  for (uint8_t i = 0; i < ALLOWED_PINS_COUNT; i++) {
    if (ALLOWED_PINS[i] == pin) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Data model
// ---------------------------------------------------------------------------
struct RelayConfig {
  uint8_t id;          // stable id, not reused after delete
  String  name;
  uint8_t pin;          // Arduino (ATmega328P) digital pin this relay is wired to
  bool    momentary;    // false = latching ON/OFF, true = pulses then auto-off
  uint16_t pulseMs;     // pulse length in ms, only used when momentary == true
  bool    state;        // last known logical state (ON/OFF)

  // Scheduling (optional; a slot only fires while its *Enabled flag is set).
  bool    schedOnEnabled;
  uint8_t schedOnHour;
  uint8_t schedOnMinute;
  bool    schedOffEnabled;   // ignored for momentary relays (they auto-off)
  uint8_t schedOffHour;
  uint8_t schedOffMinute;
  uint8_t schedDaysMask;     // bit0=Sun .. bit6=Sat, 0x7F = every day
};

struct AppConfig {
  // Station (home WiFi) credentials
  String wifiSsid;
  String wifiPass;

  // Access point (always-on fallback / local config network)
  String apSsid;
  String apPass;
  bool   apHidden;

  // Static IP (used only when staticIpEnabled == true, otherwise DHCP)
  bool   staticIpEnabled;
  String ip;
  String gateway;
  String subnet;
  String dns;

  // MQTT
  String mqttServer;
  uint16_t mqttPort;
  String mqttPrefix;

  // Scheduling / time sync
  bool    ntpEnabled;
  int16_t utcOffsetMinutes;  // e.g. 120 for UTC+2. Include DST manually if you observe it.
  String  ntpServer;

  // Relays
  uint8_t relayCount;
  RelayConfig relays[MAX_RELAYS];
  uint8_t nextRelayId;
};

extern AppConfig cfg;

// ---------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------
inline void setDefaultConfig() {
  cfg.wifiSsid = "";
  cfg.wifiPass = "";

  cfg.apSsid = "ESP8266-Garage";
  cfg.apPass = "12345678";
  cfg.apHidden = false;

  cfg.staticIpEnabled = false;
  cfg.ip = "192.168.1.36";
  cfg.gateway = "192.168.1.1";
  cfg.subnet = "255.255.255.0";
  cfg.dns = "8.8.8.8";

  cfg.mqttServer = "";
  cfg.mqttPort = 1883;
  cfg.mqttPrefix = "garage";

  cfg.ntpEnabled = false;
  cfg.utcOffsetMinutes = 120;
  cfg.ntpServer = "pool.ntp.org";

  cfg.relayCount = 2;
  cfg.relays[0] = {1, "Relay 1", 2, false, 800, false, false, 0, 0, false, 0, 0, 0x7F};
  cfg.relays[1] = {2, "Relay 2", 3, false, 800, false, false, 0, 0, false, 0, 0, 0x7F};
  cfg.nextRelayId = 3;
}

// ---------------------------------------------------------------------------
// Load / Save (LittleFS + ArduinoJson)
// ---------------------------------------------------------------------------
inline bool saveConfig() {
  DynamicJsonDocument doc(6144);

  doc["wifiSsid"] = cfg.wifiSsid;
  doc["wifiPass"] = cfg.wifiPass;
  doc["apSsid"] = cfg.apSsid;
  doc["apPass"] = cfg.apPass;
  doc["apHidden"] = cfg.apHidden;
  doc["staticIpEnabled"] = cfg.staticIpEnabled;
  doc["ip"] = cfg.ip;
  doc["gateway"] = cfg.gateway;
  doc["subnet"] = cfg.subnet;
  doc["dns"] = cfg.dns;
  doc["mqttServer"] = cfg.mqttServer;
  doc["mqttPort"] = cfg.mqttPort;
  doc["mqttPrefix"] = cfg.mqttPrefix;
  doc["ntpEnabled"] = cfg.ntpEnabled;
  doc["utcOffsetMinutes"] = cfg.utcOffsetMinutes;
  doc["ntpServer"] = cfg.ntpServer;
  doc["nextRelayId"] = cfg.nextRelayId;

  JsonArray arr = doc.createNestedArray("relays");
  for (uint8_t i = 0; i < cfg.relayCount; i++) {
    JsonObject r = arr.createNestedObject();
    r["id"] = cfg.relays[i].id;
    r["name"] = cfg.relays[i].name;
    r["pin"] = cfg.relays[i].pin;
    r["momentary"] = cfg.relays[i].momentary;
    r["pulseMs"] = cfg.relays[i].pulseMs;
    r["state"] = cfg.relays[i].state;
    r["schedOnEnabled"] = cfg.relays[i].schedOnEnabled;
    r["schedOnHour"] = cfg.relays[i].schedOnHour;
    r["schedOnMinute"] = cfg.relays[i].schedOnMinute;
    r["schedOffEnabled"] = cfg.relays[i].schedOffEnabled;
    r["schedOffHour"] = cfg.relays[i].schedOffHour;
    r["schedOffMinute"] = cfg.relays[i].schedOffMinute;
    r["schedDaysMask"] = cfg.relays[i].schedDaysMask;
  }

  File f = LittleFS.open(CONFIG_PATH, "w");
  if (!f) return false;
  bool ok = serializeJson(doc, f) > 0;
  f.close();
  return ok;
}

inline bool loadConfig() {
  if (!LittleFS.exists(CONFIG_PATH)) return false;
  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) return false;

  DynamicJsonDocument doc(6144);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  cfg.wifiSsid = doc["wifiSsid"] | "";
  cfg.wifiPass = doc["wifiPass"] | "";
  cfg.apSsid = doc["apSsid"] | "ESP8266-Garage";
  cfg.apPass = doc["apPass"] | "12345678";
  cfg.apHidden = doc["apHidden"] | false;
  cfg.staticIpEnabled = doc["staticIpEnabled"] | false;
  cfg.ip = doc["ip"] | "192.168.1.36";
  cfg.gateway = doc["gateway"] | "192.168.1.1";
  cfg.subnet = doc["subnet"] | "255.255.255.0";
  cfg.dns = doc["dns"] | "8.8.8.8";
  cfg.mqttServer = doc["mqttServer"] | "";
  cfg.mqttPort = doc["mqttPort"] | 1883;
  cfg.mqttPrefix = doc["mqttPrefix"] | "garage";
  cfg.ntpEnabled = doc["ntpEnabled"] | false;
  cfg.utcOffsetMinutes = doc["utcOffsetMinutes"] | 120;
  cfg.ntpServer = doc["ntpServer"] | "pool.ntp.org";
  cfg.nextRelayId = doc["nextRelayId"] | 1;

  JsonArray arr = doc["relays"].as<JsonArray>();
  cfg.relayCount = 0;
  for (JsonObject r : arr) {
    if (cfg.relayCount >= MAX_RELAYS) break;
    RelayConfig rc;
    rc.id = r["id"] | (cfg.relayCount + 1);
    rc.name = r["name"] | String("Relay ") + String(cfg.relayCount + 1);
    rc.pin = r["pin"] | ALLOWED_PINS[0];
    rc.momentary = r["momentary"] | false;
    rc.pulseMs = r["pulseMs"] | 800;
    rc.state = r["state"] | false;
    rc.schedOnEnabled = r["schedOnEnabled"] | false;
    rc.schedOnHour = r["schedOnHour"] | 0;
    rc.schedOnMinute = r["schedOnMinute"] | 0;
    rc.schedOffEnabled = r["schedOffEnabled"] | false;
    rc.schedOffHour = r["schedOffHour"] | 0;
    rc.schedOffMinute = r["schedOffMinute"] | 0;
    rc.schedDaysMask = r["schedDaysMask"] | 0x7F;
    cfg.relays[cfg.relayCount++] = rc;
  }

  if (cfg.relayCount == 0) {
    // Corrupt/empty relay list — fall back to two safe defaults.
    cfg.relayCount = 2;
    cfg.relays[0] = {1, "Relay 1", 2, false, 800, false, false, 0, 0, false, 0, 0, 0x7F};
    cfg.relays[1] = {2, "Relay 2", 3, false, 800, false, false, 0, 0, false, 0, 0, 0x7F};
    cfg.nextRelayId = 3;
  }

  return true;
}

inline int findRelayIndexById(uint8_t id) {
  for (uint8_t i = 0; i < cfg.relayCount; i++) {
    if (cfg.relays[i].id == id) return i;
  }
  return -1;
}

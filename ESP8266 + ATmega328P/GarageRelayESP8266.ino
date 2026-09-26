/*
  GarageRelayESP8266.ino
  ------------------------------------------------------------------------
  Web-controlled relay panel for the ATmega328P + ESP8266 hybrid board
  (e.g. "UNO WiFi R3" style boards, see README.md for the DIP switch
  settings and wiring this sketch expects).

  Role split:
    - This chip (ESP8266) owns WiFi, the web UI, MQTT, OTA updates and
      the scheduler. It has NO relays wired to it directly.
    - The ATmega328P owns the actual relay GPIOs and runs
      RelayController.ino, receiving simple text commands over its
      SoftwareSerial link (pins 10/11) to switch relays on/off/pulse.

  IMPORTANT: This sketch uses the ESP8266's hardware Serial exclusively
  to talk to the ATmega328P (per the board's wiring). Do not add
  Serial.print() debug lines while the boards are linked -- they will be
  interpreted as protocol commands by the ATmega. Use a USB-serial
  monitor on the ESP8266 only while switches are set to the "upload/
  connect to ESP8266" position (Serial disconnected from the ATmega).

  Link health: this sketch pings the ATmega328P every few seconds and
  tracks whether it's still answering (mcuLinked, shown on the Home
  page). If the ATmega announces "READY" -- sent right after it boots,
  and repeated until it hears back -- this sketch immediately re-sends
  the full relay config. That specifically covers the ATmega resetting
  on its own (brownout, EMI from switching a relay, static) while this
  chip stays powered and running, which previously left the two chips
  silently out of sync until the whole board was power-cycled.

  Libraries needed (Arduino IDE Library Manager):
    - ESP8266 core (board: "Generic ESP8266 Module") -- includes ArduinoOTA
    - ArduinoJson (v6.x)
    - PubSubClient  (by Nick O'Leary)
    - LittleFS (bundled with the ESP8266 core)
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <time.h>
#include "config.h"
#include "webpages.h"

AppConfig cfg;
ESP8266WebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

const uint32_t MCU_BAUD = 9600;
unsigned long lastMqttAttempt = 0;
unsigned long lastWifiCheck = 0;
String serialBuffer;

// ---------------------------------------------------------------------------
// Serial protocol to the ATmega328P
// ---------------------------------------------------------------------------
void mcuSend(const String &line) {
  Serial.print(line);
  Serial.print('\n');
}

void mcuSyncConfig() {
  String pins;
  for (uint8_t i = 0; i < cfg.relayCount; i++) {
    if (i) pins += ',';
    pins += String(cfg.relays[i].pin);
  }
  mcuSend("CFG:" + String(cfg.relayCount) + ":" + pins);
  // Restore last known logical state for latching relays only.
  for (uint8_t i = 0; i < cfg.relayCount; i++) {
    if (!cfg.relays[i].momentary) {
      mcuSend("SET:" + String(i) + ":" + String(cfg.relays[i].state ? 1 : 0));
    }
  }
}

void mcuSetRelay(uint8_t idx, bool on) {
  mcuSend("SET:" + String(idx) + ":" + String(on ? 1 : 0));
}

// uint32_t (not uint16_t) so this can also carry the much longer ad-hoc
// timer durations from /relay/timer (minutes, not just a short momentary
// pulse in ms) -- up to about 49 days' worth of milliseconds either way.
void mcuPulseRelay(uint8_t idx, uint32_t ms) {
  mcuSend("PULSE:" + String(idx) + ":" + String(ms));
}

void mcuAll(bool on) {
  mcuSend("ALL:" + String(on ? 1 : 0));
}

// ---------------------------------------------------------------------------
// Link health -- periodic PING/PONG plus reacting to the ATmega's own
// "READY" boot announcement. See header comment above for why this matters.
// ---------------------------------------------------------------------------
bool mcuLinked = false;
unsigned long lastPingSentAt = 0;
unsigned long lastPongAt = 0;
const unsigned long MCU_PING_INTERVAL = 4000;
const unsigned long MCU_LINK_TIMEOUT = 9000;

// Drains any lines the ATmega has sent back (ACKs, READY, PONG, or STATE:
// reports if you add physical buttons on the ATmega side later).
void pollMcuSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      serialBuffer.trim();
      if (serialBuffer == "READY") {
        // The ATmega just booted (first power-up, or it reset on its own).
        // Re-push the full config so the two chips agree on relay count,
        // pin map and state again.
        mcuLinked = true;
        lastPongAt = millis();
        mcuSyncConfig();
      } else if (serialBuffer == "PONG") {
        mcuLinked = true;
        lastPongAt = millis();
      } else if (serialBuffer.startsWith("STATE:")) {
        // Format: STATE:<index>:<0|1>  -- a local override happened on
        // the ATmega side. Mirror it into our config + MQTT.
        int c1 = serialBuffer.indexOf(':', 6);
        if (c1 > 0) {
          int idx = serialBuffer.substring(6, c1).toInt();
          bool on = serialBuffer.substring(c1 + 1).toInt() == 1;
          if (idx >= 0 && idx < cfg.relayCount) {
            cfg.relays[idx].state = on;
            saveConfig();
            publishRelayStatus(idx);
          }
        }
      }
      serialBuffer = "";
    } else if (c != '\r') {
      serialBuffer += c;
      if (serialBuffer.length() > 64) serialBuffer = ""; // guard against garbage
    }
  }

  unsigned long now = millis();
  if (now - lastPingSentAt > MCU_PING_INTERVAL) {
    lastPingSentAt = now;
    mcuSend("PING");
  }
  if (mcuLinked && now - lastPongAt > MCU_LINK_TIMEOUT) {
    mcuLinked = false; // stopped answering -- shown on the Home page
  }
}

// ---------------------------------------------------------------------------
// Relay control (single source of truth used by both the web UI and MQTT)
// ---------------------------------------------------------------------------
void publishRelayStatus(uint8_t idx) {
  if (!mqttClient.connected() || cfg.mqttServer.length() == 0) return;
  RelayConfig &r = cfg.relays[idx];
  String topic = cfg.mqttPrefix + "/status/relay" + String(r.id);
  mqttClient.publish(topic.c_str(), r.state ? "ON" : "OFF", true);
}

void setRelayState(uint8_t idx, bool on) {
  if (idx >= cfg.relayCount) return;
  RelayConfig &r = cfg.relays[idx];
  if (r.momentary) {
    if (on) mcuPulseRelay(idx, r.pulseMs); // OFF requests are ignored for pulse relays
    return;
  }
  r.state = on;
  mcuSetRelay(idx, on);
  saveConfig();
  publishRelayStatus(idx);
}

void setAllRelays(bool on) {
  for (uint8_t i = 0; i < cfg.relayCount; i++) {
    if (!cfg.relays[i].momentary) setRelayState(i, on);
  }
}

// ---------------------------------------------------------------------------
// Scheduling (NTP-based)
// ---------------------------------------------------------------------------
int lastScheduleMinute = -1;

String currentTimeStatus() {
  if (!cfg.ntpEnabled) return "Not enabled";
  time_t now = time(nullptr);
  if (now < 8 * 3600 * 24 * 365) return "Enabled, waiting for time sync...";
  struct tm t;
  localtime_r(&now, &t);
  char buf[24];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  return String("Synced -- ") + buf;
}

void checkSchedules() {
  if (!cfg.ntpEnabled) return;
  time_t now = time(nullptr);
  if (now < 8 * 3600 * 24 * 365) return; // clock not synced yet (still near epoch)

  struct tm t;
  localtime_r(&now, &t);
  if (t.tm_min == lastScheduleMinute) return; // only act once per minute
  lastScheduleMinute = t.tm_min;

  uint8_t dayBit = 1 << t.tm_wday; // tm_wday: 0=Sunday .. 6=Saturday

  for (uint8_t i = 0; i < cfg.relayCount; i++) {
    RelayConfig &r = cfg.relays[i];
    if (!(r.schedDaysMask & dayBit)) continue;

    if (r.schedOnEnabled && r.schedOnHour == t.tm_hour && r.schedOnMinute == t.tm_min) {
      setRelayState(i, true);
    }
    if (!r.momentary && r.schedOffEnabled &&
        r.schedOffHour == t.tm_hour && r.schedOffMinute == t.tm_min) {
      setRelayState(i, false);
    }
  }
}

void parseHHMM(const String &s, uint8_t &h, uint8_t &m) {
  int c = s.indexOf(':');
  if (c < 0) { h = 0; m = 0; return; }
  h = (uint8_t)s.substring(0, c).toInt();
  m = (uint8_t)s.substring(c + 1).toInt();
}

// ---------------------------------------------------------------------------
// MQTT
// ---------------------------------------------------------------------------
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String t = String(topic);
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  bool on = msg.equalsIgnoreCase("ON") || msg == "1";

  if (t == cfg.mqttPrefix + "/control/all") {
    setAllRelays(on);
    return;
  }
  String prefixPart = cfg.mqttPrefix + "/control/relay";
  if (t.startsWith(prefixPart)) {
    uint8_t id = t.substring(prefixPart.length()).toInt();
    int idx = findRelayIndexById(id);
    if (idx >= 0) setRelayState(idx, on);
  }
}

void mqttSubscribeAll() {
  mqttClient.subscribe((cfg.mqttPrefix + "/control/all").c_str());
  for (uint8_t i = 0; i < cfg.relayCount; i++) {
    String t = cfg.mqttPrefix + "/control/relay" + String(cfg.relays[i].id);
    mqttClient.subscribe(t.c_str());
  }
}

void mqttReconnect() {
  if (cfg.mqttServer.length() == 0) return;
  if (WiFi.status() != WL_CONNECTED) return; // don't even try without a WiFi link
  if (millis() - lastMqttAttempt < 5000) return;
  lastMqttAttempt = millis();
  String clientId = "esp8266-" + String(ESP.getChipId(), HEX);
  // mqttClient.connect() is blocking -- server.handleClient() can't run while
  // it's in progress. setSocketTimeout() in setup() caps how long that can be.
  if (mqttClient.connect(clientId.c_str())) {
    mqttSubscribeAll();
    for (uint8_t i = 0; i < cfg.relayCount; i++) publishRelayStatus(i);
  }
}

// ---------------------------------------------------------------------------
// Web routes
// ---------------------------------------------------------------------------
void redirectHome() {
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleRoot() { server.send(200, "text/html", pageHome(mcuLinked)); }

void handleRelay() {
  if (!server.hasArg("id")) { redirectHome(); return; }
  int idx = findRelayIndexById(server.arg("id").toInt());
  bool on = server.arg("state") == "1";
  if (idx >= 0) setRelayState(idx, on);
  redirectHome();
}

void handleAll() {
  setAllRelays(server.arg("state") == "1");
  redirectHome();
}

// Toggle endpoints: flip based on the relay's actual current state, so one
// static shortcut (no "state" parameter needed) works as an ON/OFF button
// instead of needing a separate shortcut for each direction.
void handleRelayToggle() {
  int idx = findRelayIndexById(server.arg("id").toInt());
  if (idx >= 0) {
    if (cfg.relays[idx].momentary) {
      setRelayState(idx, true); // momentary relays only have "trigger"
    } else {
      setRelayState(idx, !cfg.relays[idx].state);
    }
  }
  redirectHome();
}

void handleAllToggle() {
  bool anyOn = false;
  for (uint8_t i = 0; i < cfg.relayCount; i++) {
    if (!cfg.relays[i].momentary && cfg.relays[i].state) { anyOn = true; break; }
  }
  setAllRelays(!anyOn); // any relay ON -> turn everything OFF, else turn everything ON
  redirectHome();
}

// Ad-hoc "run for N minutes" timer, started from the Home page. Latching
// relays only -- momentary relays already have their own short Trigger
// pulse (fixed pulseMs, set on the Edit Relays page).
void handleRelayTimer() {
  int idx = findRelayIndexById(server.arg("id").toInt());
  if (idx < 0) { redirectHome(); return; }
  RelayConfig &r = cfg.relays[idx];
  if (r.momentary) { redirectHome(); return; }

  long minutes = server.arg("minutes").toInt();
  if (minutes < 1) minutes = 1;
  if (minutes > 1440) minutes = 1440; // 24h sanity cap -- use Scheduling for longer/recurring runs

  // Deliberately NOT saved to flash here: the countdown itself runs on the
  // ATmega (RelayController.ino), so it keeps going even if this chip
  // reboots mid-timer. Only the in-memory state is set now, for an
  // accurate Home page right away -- the persisted state and the MQTT
  // "OFF" publish happen for real once the ATmega reports the pulse ended
  // (a STATE: line, handled in pollMcuSerial()). That also means if this
  // chip *does* reboot mid-timer, it comes back up believing the relay is
  // still off (its last saved state) and re-asserts that -- a reboot
  // fails safe to OFF instead of leaving the relay stuck on.
  r.state = true;
  mcuPulseRelay(idx, (uint32_t)minutes * 60000UL);
  publishRelayStatus(idx);
  redirectHome();
}

void handleWifiGet() {
  String status = WiFi.status() == WL_CONNECTED ? "Connected" : "Not connected";
  server.send(200, "text/html",
              pageWiFiSettings("", status, WiFi.SSID(), WiFi.localIP().toString()));
}

void handleWifiPost() {
  cfg.wifiSsid = server.arg("ssid");
  cfg.wifiPass = server.arg("pass");
  saveConfig();
  server.send(200, "text/html",
              htmlHeader("Saving...") +
              "<div class='box'>Saved. Restarting and connecting to " +
              htmlEscape(cfg.wifiSsid) + "...</div>" + htmlFooter());
  delay(300);
  ESP.restart();
}

void handleApSettingsPost() {
  cfg.apSsid = server.arg("ap_ssid").length() ? server.arg("ap_ssid") : cfg.apSsid;
  if (server.arg("ap_pass").length() >= 8) cfg.apPass = server.arg("ap_pass");
  cfg.apHidden = server.hasArg("ap_hidden");
  saveConfig();
  WiFi.softAP(cfg.apSsid.c_str(), cfg.apPass.c_str(), 1, cfg.apHidden ? 1 : 0);
  server.sendHeader("Location", "/wifi");
  server.send(303);
}

void handleClearWifiPost() {
  cfg.wifiSsid = "";
  cfg.wifiPass = "";
  saveConfig();
  server.send(200, "text/html",
              htmlHeader("Restarting...") + "<div class='box'>Cleared. Restarting...</div>" + htmlFooter());
  delay(300);
  ESP.restart();
}

void handleCurrentIpGet() {
  String connType = cfg.staticIpEnabled ? "Static" : "DHCP";
  server.send(200, "text/html",
              pageCurrentIP("", connType, WiFi.localIP().toString(),
                             WiFi.gatewayIP().toString(), WiFi.subnetMask().toString(),
                             WiFi.macAddress()));
}

void handleCurrentIpPost() {
  cfg.staticIpEnabled = server.hasArg("static_enabled");
  if (server.arg("ip").length()) cfg.ip = server.arg("ip");
  if (server.arg("gateway").length()) cfg.gateway = server.arg("gateway");
  if (server.arg("subnet").length()) cfg.subnet = server.arg("subnet");
  if (server.arg("dns").length()) cfg.dns = server.arg("dns");
  saveConfig();
  server.send(200, "text/html",
              htmlHeader("Restarting...") + "<div class='box'>Saved. Restarting...</div>" + htmlFooter());
  delay(300);
  ESP.restart();
}

void handleClearIpPost() {
  cfg.staticIpEnabled = false;
  saveConfig();
  server.send(200, "text/html",
              htmlHeader("Restarting...") + "<div class='box'>Cleared. Restarting...</div>" + htmlFooter());
  delay(300);
  ESP.restart();
}

void handleMqttGet() { server.send(200, "text/html", pageMQTTSettings("")); }

void handleMqttPost() {
  cfg.mqttServer = server.arg("server");
  cfg.mqttPrefix = server.arg("prefix").length() ? server.arg("prefix") : cfg.mqttPrefix;
  cfg.mqttPort = server.arg("port").toInt() ? server.arg("port").toInt() : 1883;
  saveConfig();
  server.send(200, "text/html",
              htmlHeader("Restarting...") + "<div class='box'>Saved. Restarting...</div>" + htmlFooter());
  delay(300);
  ESP.restart();
}

void handleNamesGet() { server.send(200, "text/html", pageEditNames("")); }

void handleRelayUpdate() {
  int idx = findRelayIndexById(server.arg("id").toInt());
  if (idx < 0) { server.send(200, "text/html", pageEditNames("Relay not found.")); return; }
  uint8_t pin = server.arg("pin").toInt();
  if (!isAllowedPin(pin)) { server.send(200, "text/html", pageEditNames("Invalid pin.")); return; }
  for (uint8_t j = 0; j < cfg.relayCount; j++) {
    if ((int)j != idx && cfg.relays[j].pin == pin) {
      server.send(200, "text/html", pageEditNames("That pin is already used by another relay."));
      return;
    }
  }
  cfg.relays[idx].name = server.arg("name").length() ? server.arg("name") : cfg.relays[idx].name;
  cfg.relays[idx].pin = pin;
  cfg.relays[idx].momentary = server.hasArg("momentary");
  uint16_t pulse = server.arg("pulse_ms").toInt();
  cfg.relays[idx].pulseMs = pulse >= 100 ? pulse : 800;
  saveConfig();
  mcuSyncConfig();
  server.sendHeader("Location", "/names");
  server.send(303);
}

void handleRelayAdd() {
  if (cfg.relayCount >= MAX_RELAYS) { server.send(200, "text/html", pageEditNames("Relay limit reached.")); return; }
  uint8_t pin = server.arg("pin").toInt();
  if (!isAllowedPin(pin)) { server.send(200, "text/html", pageEditNames("Invalid pin.")); return; }
  for (uint8_t j = 0; j < cfg.relayCount; j++) {
    if (cfg.relays[j].pin == pin) {
      server.send(200, "text/html", pageEditNames("That pin is already used by another relay."));
      return;
    }
  }
  String name = server.arg("name").length() ? server.arg("name") : "Relay";
  uint16_t pulse = server.arg("pulse_ms").toInt();
  RelayConfig r = {cfg.nextRelayId++, name, pin, server.hasArg("momentary"),
                    (uint16_t)(pulse >= 100 ? pulse : 800), false,
                    false, 0, 0, false, 0, 0, 0x7F};
  cfg.relays[cfg.relayCount++] = r;
  saveConfig();
  mcuSyncConfig();
  server.sendHeader("Location", "/names");
  server.send(303);
}

void handleRelayDelete() {
  int idx = findRelayIndexById(server.arg("id").toInt());
  if (idx >= 0) {
    for (uint8_t j = idx; j < cfg.relayCount - 1; j++) cfg.relays[j] = cfg.relays[j + 1];
    cfg.relayCount--;
    saveConfig();
    mcuSyncConfig();
  }
  server.sendHeader("Location", "/names");
  server.send(303);
}

void handleScheduleGet() {
  server.send(200, "text/html", pageSchedule("", currentTimeStatus()));
}

void handleScheduleNtpPost() {
  cfg.ntpEnabled = server.hasArg("ntp_enabled");
  if (server.arg("utc_offset").length()) cfg.utcOffsetMinutes = server.arg("utc_offset").toInt();
  if (server.arg("ntp_server").length()) cfg.ntpServer = server.arg("ntp_server");
  saveConfig();
  if (cfg.ntpEnabled) {
    configTime(cfg.utcOffsetMinutes * 60, 0, cfg.ntpServer.c_str());
  }
  server.sendHeader("Location", "/schedule");
  server.send(303);
}

void handleScheduleRelayPost() {
  int idx = findRelayIndexById(server.arg("id").toInt());
  if (idx < 0) { server.sendHeader("Location", "/schedule"); server.send(303); return; }
  RelayConfig &r = cfg.relays[idx];

  r.schedOnEnabled = server.hasArg("on_enabled");
  parseHHMM(server.arg("on_time"), r.schedOnHour, r.schedOnMinute);

  if (!r.momentary) {
    r.schedOffEnabled = server.hasArg("off_enabled");
    parseHHMM(server.arg("off_time"), r.schedOffHour, r.schedOffMinute);
  }

  uint8_t mask = 0;
  const char *dayArgs[7] = {"d0", "d1", "d2", "d3", "d4", "d5", "d6"};
  for (uint8_t i = 0; i < 7; i++) {
    if (server.hasArg(dayArgs[i])) mask |= (1 << i);
  }
  r.schedDaysMask = mask;

  saveConfig();
  server.sendHeader("Location", "/schedule");
  server.send(303);
}

void handleRestart() {
  server.send(200, "text/html",
              htmlHeader("Restarting...") + "<div class='box'>Restarting...</div>" + htmlFooter());
  delay(300);
  ESP.restart();
}

void handleNotFound() { server.send(404, "text/plain", "Not found"); }

// ---------------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(MCU_BAUD); // link to ATmega328P -- see header comment above

  LittleFS.begin();
  if (!loadConfig()) {
    setDefaultConfig();
    saveConfig();
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(cfg.apSsid.c_str(), cfg.apPass.c_str(), 1, cfg.apHidden ? 1 : 0);

  if (cfg.wifiSsid.length()) {
    if (cfg.staticIpEnabled) {
      IPAddress ip, gw, mask, dns;
      ip.fromString(cfg.ip);
      gw.fromString(cfg.gateway);
      mask.fromString(cfg.subnet);
      dns.fromString(cfg.dns);
      WiFi.config(ip, gw, mask, dns);
    }
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) delay(200);
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/relay", HTTP_POST, handleRelay);
  server.on("/all", HTTP_POST, handleAll);
  server.on("/relay/toggle", HTTP_POST, handleRelayToggle);
  server.on("/all/toggle", HTTP_POST, handleAllToggle);
  server.on("/relay/timer", HTTP_POST, handleRelayTimer);
  server.on("/wifi", HTTP_GET, handleWifiGet);
  server.on("/wifi", HTTP_POST, handleWifiPost);
  server.on("/apsettings", HTTP_POST, handleApSettingsPost);
  server.on("/clearwifi", HTTP_POST, handleClearWifiPost);
  server.on("/currentip", HTTP_GET, handleCurrentIpGet);
  server.on("/currentip", HTTP_POST, handleCurrentIpPost);
  server.on("/clearip", HTTP_POST, handleClearIpPost);
  server.on("/mqtt", HTTP_GET, handleMqttGet);
  server.on("/mqtt", HTTP_POST, handleMqttPost);
  server.on("/names", HTTP_GET, handleNamesGet);
  server.on("/relay/update", HTTP_POST, handleRelayUpdate);
  server.on("/relay/add", HTTP_POST, handleRelayAdd);
  server.on("/relay/delete", HTTP_POST, handleRelayDelete);
  server.on("/schedule", HTTP_GET, handleScheduleGet);
  server.on("/schedule/ntp", HTTP_POST, handleScheduleNtpPost);
  server.on("/schedule/relay", HTTP_POST, handleScheduleRelayPost);
  server.on("/restart", HTTP_POST, handleRestart);
  server.onNotFound(handleNotFound);
  server.begin();

  // OTA updates -- in the Arduino IDE, once this device is online its name
  // will show up under Tools > Port as a network port. Uploading over it
  // will prompt for the AP password set on the WiFi Settings page.
  ArduinoOTA.setHostname("garage-esp8266");
  ArduinoOTA.setPassword(cfg.apPass.c_str());
  ArduinoOTA.begin();

  if (cfg.ntpEnabled) {
    configTime(cfg.utcOffsetMinutes * 60, 0, cfg.ntpServer.c_str());
  }

  delay(500); // give the ATmega time to finish its own setup()
  mcuSyncConfig();

  if (cfg.mqttServer.length()) {
    mqttClient.setServer(cfg.mqttServer.c_str(), cfg.mqttPort);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setSocketTimeout(2); // seconds -- caps how long a bad broker can block the web server
  }
}

void loop() {
  server.handleClient();
  ArduinoOTA.handle();
  pollMcuSerial();
  checkSchedules();

  if (cfg.mqttServer.length()) {
    if (!mqttClient.connected()) {
      mqttReconnect();
    } else {
      mqttClient.loop();
    }
  }

  if (cfg.wifiSsid.length() && millis() - lastWifiCheck > 30000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
  }
}

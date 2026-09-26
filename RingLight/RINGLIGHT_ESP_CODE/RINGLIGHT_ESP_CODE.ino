/*
  ==========================================================
   Smart Relay Controller - ESP8266
  ==========================================================
  Features:
   1) Add/remove relay buttons with a custom pin number for
      each one (persisted in LittleFS, survives power loss)
   2) A hotspot (Access Point) that appears automatically
      when the home WiFi connection fails, and disappears
      automatically once it reconnects
   3) A WiFi settings page (SSID + password) reachable while
      connected to the hotspot, so you can change the home
      network without re-flashing the device
   4) Every relay endpoint accepts both GET and POST, plus a
      /relay/toggle endpoint, so it works directly with apps
      like "HTTP Shortcuts" (Android)

  Required libraries (Arduino IDE Library Manager):
   - ESP8266WiFi        (bundled with ESP8266 core)
   - ESP8266WebServer   (bundled with ESP8266 core)
   - LittleFS           (bundled with ESP8266 core - choose "LittleFS", not SPIFFS)
   - ArduinoJson (v6 or v7) - install from Library Manager

  Note on pin numbers: the pin numbers you enter on the web
  page belong to the second board (Arduino) that the relays
  are physically wired to, not the ESP8266 itself.

  ==========================================================
   HTTP Shortcuts examples (Android app)
  ==========================================================
   Turn a relay ON   : POST http://<device-ip>/relay/on      body: pin=5
   Turn a relay OFF  : POST http://<device-ip>/relay/off     body: pin=5
   Toggle a relay    : POST http://<device-ip>/relay/toggle  body: pin=5
   (GET with ?pin=5 also works for all three, if you prefer GET shortcuts)
  ==========================================================
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <vector>

// ================== Defaults used if no config file exists yet ==================
const char* DEFAULT_SSID = "We";
const char* DEFAULT_PASS = "20121975.";

// ================== Hotspot (Access Point) settings ==================
const char* AP_SSID = "RingLight-Setup";
const char* AP_PASS = "12345678"; // must be at least 8 characters

ESP8266WebServer server(80);

struct RelayInfo {
  String name;
  int pin;
  bool state; // true = ON, false = OFF (tracked so /relay/toggle works)
};

std::vector<RelayInfo> relays;

String currentSSID = DEFAULT_SSID;
String currentPASS = DEFAULT_PASS;

bool apActive = false;
unsigned long lastWiFiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 5000; // how often (ms) to re-check WiFi status

// ================== Main control page ==================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Smart Light Control</title>
<style>
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body { background: #0f0f1a; font-family: Arial, sans-serif; display: flex; justify-content: center; padding: 20px; }
  .container { background: #16213e; border-radius: 30px; padding: 30px; text-align: center; width: 100%; max-width: 420px; box-shadow: 0 15px 40px rgba(0,0,0,0.4); }
  h1 { color: white; font-size: 28px; margin-bottom: 6px; }
  .subtitle { color: #888; font-size: 13px; margin-bottom: 20px; }
  .relayCard { background: #0f0f1a; border-radius: 20px; padding: 16px; margin: 14px 0; }
  .relayName { color: #ddd; font-size: 16px; margin-bottom: 10px; font-weight: bold; }
  .relayPin { color: #666; font-size: 11px; margin-bottom: 10px; }
  .row { display: flex; gap: 10px; }
  button { flex: 1; padding: 16px; font-size: 18px; font-weight: bold; border: none; border-radius: 40px; cursor: pointer; transition: 0.2s; }
  button:active { transform: scale(0.96); }
  .btn-on { background: #4CAF50; color: white; box-shadow: 0 4px 0 #2e7d32; }
  .btn-off { background: #f44336; color: white; box-shadow: 0 4px 0 #c62828; }
  .btn-del { background: #555; color: white; flex: 0 0 46px; box-shadow: 0 4px 0 #333; }
  .addBox { background: #0f0f1a; border-radius: 20px; padding: 16px; margin-top: 20px; }
  .addBox input { width: 100%; padding: 12px; margin: 6px 0; border-radius: 10px; border: none; background: #1c1c2e; color: white; font-size: 14px; }
  .addBox button { width: 100%; margin-top: 8px; background: #2196F3; color: white; box-shadow: 0 4px 0 #1565C0; }
  .status-box { background: #0f0f1a; padding: 12px; border-radius: 16px; margin-top: 20px; font-family: monospace; font-size: 13px; color: #4CAF50; }
  .apWarning { display: none; background: #4a3b1a; color: #ffcf5c; padding: 10px; border-radius: 14px; font-size: 12px; margin-bottom: 16px; }
  .footer a { color: #888; font-size: 11px; }
</style>
</head>
<body>
<div class="container">
  <h1>Smart Light</h1>
  <div class="subtitle">WiFi Remote Control</div>

  <div class="apWarning" id="apWarning">&#9888; Device is running in hotspot mode (no home WiFi connection)</div>

  <div id="relayList"></div>

  <div class="addBox">
    <div style="color:#ddd; font-size:14px; margin-bottom:8px;">Add a new button</div>
    <input type="text" id="newName" placeholder="Button name (e.g. Ceiling Light)">
    <input type="number" id="newPin" placeholder="Pin number (e.g. 5)">
    <button onclick="addRelay()">Add</button>
  </div>

  <div class="status-box" id="statusMsg">System Ready</div>
  <div class="footer" style="margin-top:14px;"><a href="/wifi">WiFi Settings</a></div>
</div>

<script>
function loadRelays() {
  fetch('/api/relays').then(r => r.json()).then(data => {
    var list = document.getElementById('relayList');
    list.innerHTML = '';
    data.relays.forEach(function(r) {
      var card = document.createElement('div');
      card.className = 'relayCard';
      card.innerHTML =
        '<div class="relayName">' + r.name + '</div>' +
        '<div class="relayPin">Pin: ' + r.pin + ' &middot; ' + (r.state ? 'ON' : 'OFF') + '</div>' +
        '<div class="row">' +
          '<button class="btn-on" onclick="setRelay(' + r.pin + ', true)">ON</button>' +
          '<button class="btn-off" onclick="setRelay(' + r.pin + ', false)">OFF</button>' +
          '<button class="btn-del" onclick="removeRelay(' + r.pin + ')">&times;</button>' +
        '</div>';
      list.appendChild(card);
    });
  });
}

function postForm(url, params) {
  var body = Object.keys(params).map(function(k) {
    return encodeURIComponent(k) + '=' + encodeURIComponent(params[k]);
  }).join('&');
  return fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: body
  });
}

function setRelay(pin, on) {
  postForm('/relay/' + (on ? 'on' : 'off'), { pin: pin })
    .then(r => r.text()).then(function(t) {
      document.getElementById('statusMsg').innerHTML = t;
      loadRelays();
    });
}

function addRelay() {
  var name = document.getElementById('newName').value.trim();
  var pin = document.getElementById('newPin').value.trim();
  if (!name || pin === '') { alert('Enter a name and a pin number'); return; }
  postForm('/api/addRelay', { name: name, pin: pin })
    .then(r => r.text()).then(function(t) {
      document.getElementById('statusMsg').innerHTML = t;
      document.getElementById('newName').value = '';
      document.getElementById('newPin').value = '';
      loadRelays();
    });
}

function removeRelay(pin) {
  if (!confirm('Remove this button?')) return;
  postForm('/api/removeRelay', { pin: pin })
    .then(r => r.text()).then(function(t) { document.getElementById('statusMsg').innerHTML = t; loadRelays(); });
}

function loadStatus() {
  fetch('/api/status').then(r => r.json()).then(s => {
    document.getElementById('apWarning').style.display = s.apActive ? 'block' : 'none';
  });
}

loadRelays();
loadStatus();
setInterval(loadStatus, 4000);
</script>
</body>
</html>
)rawliteral";

// ================== WiFi settings page ==================
const char wifi_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>WiFi Settings</title>
<style>
  * { margin:0; padding:0; box-sizing:border-box; }
  body { background:#0f0f1a; font-family:Arial, sans-serif; display:flex; justify-content:center; padding:20px; }
  .container { background:#16213e; border-radius:30px; padding:30px; width:100%; max-width:400px; }
  h1 { color:white; font-size:24px; margin-bottom:16px; text-align:center; }
  input { width:100%; padding:14px; margin:8px 0; border-radius:10px; border:none; background:#1c1c2e; color:white; font-size:15px; }
  button { width:100%; padding:16px; margin-top:12px; border:none; border-radius:40px; background:#2196F3; color:white; font-weight:bold; font-size:16px; box-shadow:0 4px 0 #1565C0; }
  a { display:block; text-align:center; margin-top:16px; color:#888; font-size:12px; }
  .msg { color:#4CAF50; text-align:center; margin-top:12px; font-size:13px; }
</style>
</head>
<body>
<div class="container">
  <h1>WiFi Settings</h1>
  <input type="text" id="ssid" placeholder="Network name (SSID)">
  <input type="password" id="pass" placeholder="Password">
  <button onclick="saveWifi()">Save &amp; Restart</button>
  <div class="msg" id="msg"></div>
  <a href="/">Back to control panel</a>
</div>
<script>
function saveWifi() {
  var s = document.getElementById('ssid').value.trim();
  var p = document.getElementById('pass').value;
  if (!s) { alert('Enter the network name'); return; }
  var body = 'ssid=' + encodeURIComponent(s) + '&pass=' + encodeURIComponent(p);
  fetch('/wifi/save', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: body
  }).then(r => r.text()).then(t => document.getElementById('msg').innerHTML = t);
}
</script>
</body>
</html>
)rawliteral";

// ================== Relay list persistence (LittleFS) ==================
void saveRelays() {
  DynamicJsonDocument doc(2048);
  JsonArray arr = doc.to<JsonArray>();
  for (auto &r : relays) {
    JsonObject o = arr.createNestedObject();
    o["name"] = r.name;
    o["pin"] = r.pin;
  }
  File f = LittleFS.open("/relays.json", "w");
  if (f) {
    serializeJson(doc, f);
    f.close();
  }
}

void loadRelays() {
  relays.clear();
  if (!LittleFS.exists("/relays.json")) return;
  File f = LittleFS.open("/relays.json", "r");
  if (!f) return;
  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return;
  for (JsonObject o : doc.as<JsonArray>()) {
    RelayInfo r;
    r.name = o["name"].as<String>();
    r.pin = o["pin"].as<int>();
    r.state = false;
    relays.push_back(r);
  }
}

int findRelayIndex(int pin) {
  for (size_t i = 0; i < relays.size(); i++) {
    if (relays[i].pin == pin) return i;
  }
  return -1;
}

// ================== WiFi credentials persistence (LittleFS) ==================
void saveWiFiConfig(const String &ssid, const String &pass) {
  DynamicJsonDocument doc(256);
  doc["ssid"] = ssid;
  doc["pass"] = pass;
  File f = LittleFS.open("/wifi.json", "w");
  if (f) {
    serializeJson(doc, f);
    f.close();
  }
}

void loadWiFiConfig() {
  currentSSID = DEFAULT_SSID;
  currentPASS = DEFAULT_PASS;
  if (!LittleFS.exists("/wifi.json")) return;
  File f = LittleFS.open("/wifi.json", "r");
  if (!f) return;
  DynamicJsonDocument doc(256);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return;
  currentSSID = doc["ssid"].as<String>();
  currentPASS = doc["pass"].as<String>();
}

// ================== WiFi connection + hotspot management ==================
void startAP() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  apActive = true;
  Serial.println("HOTSPOT ON: " + String(AP_SSID));
}

void stopAP() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive = false;
  Serial.println("HOTSPOT OFF");
}

// First connection attempt at boot (with a fixed timeout)
void initialConnect() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(currentSSID.c_str(), currentPASS.c_str());
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(400);
    Serial.print(".");
  }
  Serial.println("");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected successfully!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    apActive = false;
  } else {
    Serial.println("Could not connect - starting hotspot fallback");
    startAP();
  }
}

// Periodically checks WiFi status and opens/closes the hotspot automatically
void checkWiFiStatus() {
  if (millis() - lastWiFiCheck < WIFI_CHECK_INTERVAL) return;
  lastWiFiCheck = millis();

  bool connected = (WiFi.status() == WL_CONNECTED);

  if (connected && apActive) {
    // Back on home WiFi -> close the hotspot
    stopAP();
    Serial.print("Reconnected. IP: ");
    Serial.println(WiFi.localIP());
  } else if (!connected && !apActive) {
    // Connection problem -> open the hotspot
    startAP();
  }
}

// ================== Web server handlers ==================
// Note: server.arg("pin") reads from either a query string (GET) or a
// form-urlencoded body (POST) - ESP8266WebServer parses both the same
// way, so every handler below works with GET or POST requests, which
// is what lets it work directly with apps like HTTP Shortcuts.

void handleRoot() {
  server.send(200, "text/html", index_html);
}

void handleWifiPage() {
  server.send(200, "text/html", wifi_html);
}

void handleApiRelays() {
  DynamicJsonDocument doc(2048);
  JsonObject root = doc.to<JsonObject>();
  JsonArray arr = root.createNestedArray("relays");
  for (auto &r : relays) {
    JsonObject o = arr.createNestedObject();
    o["name"] = r.name;
    o["pin"] = r.pin;
    o["state"] = r.state;
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleApiStatus() {
  DynamicJsonDocument doc(256);
  doc["connected"] = (WiFi.status() == WL_CONNECTED);
  doc["apActive"] = apActive;
  doc["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "";
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleAddRelay() {
  if (!server.hasArg("name") || !server.hasArg("pin")) {
    server.send(400, "text/plain", "Missing data");
    return;
  }
  String name = server.arg("name");
  int pin = server.arg("pin").toInt();

  // pins 0 and 1 are reserved for the serial link between the ESP and the second board
  if (pin < 2 || pin > 13) {
    server.send(400, "text/plain", "Pin number must be between 2 and 13");
    return;
  }
  if (findRelayIndex(pin) != -1) {
    server.send(400, "text/plain", "A button is already registered on this pin");
    return;
  }

  RelayInfo r;
  r.name = name;
  r.pin = pin;
  r.state = false;
  relays.push_back(r);
  saveRelays();

  Serial.println("ADD:" + String(pin));
  server.send(200, "text/plain", "Added \"" + name + "\"");
}

void handleRemoveRelay() {
  if (!server.hasArg("pin")) {
    server.send(400, "text/plain", "Missing pin number");
    return;
  }
  int pin = server.arg("pin").toInt();
  int idx = findRelayIndex(pin);
  if (idx == -1) {
    server.send(404, "text/plain", "This button does not exist");
    return;
  }
  relays.erase(relays.begin() + idx);
  saveRelays();

  Serial.println("REMOVE:" + String(pin));
  server.send(200, "text/plain", "Removed");
}

void handleRelayOn() {
  if (!server.hasArg("pin")) { server.send(400, "text/plain", "Missing pin number"); return; }
  int pin = server.arg("pin").toInt();
  int idx = findRelayIndex(pin);
  if (idx != -1) relays[idx].state = true;
  Serial.println("ON:" + String(pin));
  server.send(200, "text/plain", "Pin " + String(pin) + " ON");
}

void handleRelayOff() {
  if (!server.hasArg("pin")) { server.send(400, "text/plain", "Missing pin number"); return; }
  int pin = server.arg("pin").toInt();
  int idx = findRelayIndex(pin);
  if (idx != -1) relays[idx].state = false;
  Serial.println("OFF:" + String(pin));
  server.send(200, "text/plain", "Pin " + String(pin) + " OFF");
}

void handleRelayToggle() {
  if (!server.hasArg("pin")) { server.send(400, "text/plain", "Missing pin number"); return; }
  int pin = server.arg("pin").toInt();
  int idx = findRelayIndex(pin);
  bool newState;
  if (idx != -1) {
    newState = !relays[idx].state;
    relays[idx].state = newState;
  } else {
    // Unknown pin (not saved as a relay button) - just default to turning it ON
    newState = true;
  }
  Serial.println(newState ? "ON:" + String(pin) : "OFF:" + String(pin));
  server.send(200, "text/plain", "Pin " + String(pin) + (newState ? " ON" : " OFF"));
}

void handleWifiSave() {
  if (!server.hasArg("ssid")) { server.send(400, "text/plain", "Missing network name"); return; }
  String ssid = server.arg("ssid");
  String pass = server.hasArg("pass") ? server.arg("pass") : "";
  saveWiFiConfig(ssid, pass);
  server.send(200, "text/plain", "Saved. Restarting now...");
  delay(1500);
  ESP.restart();
}

void setup() {
  Serial.begin(9600);
  delay(100);

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed, formatting...");
    LittleFS.format();
    LittleFS.begin();
  }

  loadWiFiConfig();
  loadRelays();

  initialConnect();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/wifi", HTTP_GET, handleWifiPage);
  server.on("/wifi/save", HTTP_ANY, handleWifiSave);
  server.on("/api/relays", HTTP_GET, handleApiRelays);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/addRelay", HTTP_ANY, handleAddRelay);
  server.on("/api/removeRelay", HTTP_ANY, handleRemoveRelay);
  server.on("/relay/on", HTTP_ANY, handleRelayOn);         // GET or POST, e.g. pin=5
  server.on("/relay/off", HTTP_ANY, handleRelayOff);       // GET or POST, e.g. pin=5
  server.on("/relay/toggle", HTTP_ANY, handleRelayToggle); // GET or POST, e.g. pin=5

  server.begin();

  // Re-send setup commands for every saved relay (in case the second board rebooted)
  for (auto &r : relays) {
    Serial.println("ADD:" + String(r.pin));
  }
}

void loop() {
  server.handleClient();
  checkWiFiStatus();
}

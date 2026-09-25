#pragma once
#include <Arduino.h>
#include "config.h"

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
inline String htmlEscape(const String &in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    switch (c) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      default:  out += c;
    }
  }
  return out;
}

inline String twoDigit(uint8_t v) {
  String s = (v < 10) ? "0" : "";
  s += String(v);
  return s;
}

// ---------------------------------------------------------------------------
// Shared CSS + nav + header/footer
// ---------------------------------------------------------------------------
inline String htmlStyle() {
  String s;
  s += F("<style>"
         ":root{--bg:#121212;--card:#1e1e1e;--nav:#262626;--text:#f2f2f2;"
         "--green:#27ae60;--green-d:#1e8449;--red:#e74c3c;--red-d:#b93225;"
         "--accent:#3498db;--orange:#e67e22;--purple:#9b59b6;--muted:#9a9a9a;}"
         "*{box-sizing:border-box;}"
         "body{background:var(--bg);color:var(--text);font-family:Segoe UI,Arial,sans-serif;"
         "margin:0;padding:0 16px 40px;}"
         "h1{text-align:center;font-size:2em;margin:24px 0;}"
         "h2{color:var(--purple);}"
         "nav{display:flex;flex-wrap:wrap;justify-content:center;gap:8px;padding:16px 0;}"
         "nav a{background:var(--nav);color:var(--text);text-decoration:none;padding:10px 16px;"
         "border-radius:8px;font-size:.95em;}"
         "nav a:hover{background:#333;}"
         ".card{background:var(--card);border-radius:14px;padding:20px;margin:14px auto;"
         "max-width:420px;text-align:center;}"
         ".card h3{margin-top:0;font-size:1.4em;}"
         ".status{margin-bottom:14px;color:var(--muted);}"
         ".status b{color:var(--text);}"
         "form{margin:0;}"
         "input[type=text],input[type=password],input[type=number],input[type=time],select{"
         "width:100%;padding:12px;margin:6px 0;border-radius:8px;border:1px solid #333;"
         "background:#2a2a2a;color:var(--text);font-size:1em;}"
         "label{display:block;text-align:left;margin-top:10px;color:var(--muted);font-size:.9em;}"
         ".btn{display:inline-block;width:100%;padding:14px;margin:6px 0;border:none;"
         "border-radius:10px;font-size:1.05em;font-weight:600;cursor:pointer;color:#fff;}"
         ".btn-green{background:var(--green);} .btn-green:active{background:var(--green-d);}"
         ".btn-red{background:var(--red);} .btn-red:active{background:var(--red-d);}"
         ".btn-blue{background:var(--accent);}"
         ".btn-orange{background:var(--orange);}"
         ".btn-purple{background:var(--purple);}"
         ".btn-grey{background:#444;}"
         ".row{display:flex;gap:8px;} .row .btn{flex:1;}"
         ".box{background:var(--card);border-radius:12px;padding:16px;max-width:420px;"
         "margin:14px auto;text-align:center;}"
         ".box-green{border:1px solid var(--green);}"
         ".info{background:#20303d;border-radius:10px;padding:12px;max-width:420px;"
         "margin:14px auto;font-size:.9em;text-align:left;}"
         ".msg{background:#20303d;border-left:4px solid var(--accent);padding:10px 14px;"
         "max-width:420px;margin:14px auto;text-align:left;border-radius:6px;}"
         ".topic{color:#7ecfff;}"
         ".daychip{display:inline-flex;align-items:center;gap:4px;width:auto!important;"
         "margin:4px 6px 0 0!important;}"
         ".linkbar{max-width:420px;margin:0 auto 14px;padding:10px 16px;border-radius:10px;"
         "background:var(--card);text-align:center;font-size:.9em;}"
         "footer{text-align:center;color:var(--muted);font-size:.8em;margin-top:30px;}"
         "hr{border:none;border-top:1px solid #333;margin:14px 0;}"
         "</style>");
  return s;
}

inline String htmlHeader(const String &title, const String &msg = "") {
  String s;
  s += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>");
  s += htmlEscape(title);
  s += F("</title>");
  s += htmlStyle();
  s += F("</head><body>"
         "<nav>"
         "<a href='/'>Home</a>"
         "<a href='/wifi'>WiFi Settings</a>"
         "<a href='/names'>Edit Relays</a>"
         "<a href='/schedule'>Scheduling</a>"
         "<a href='/mqtt'>MQTT Settings</a>"
         "</nav>");
  if (msg.length()) {
    s += F("<div class='msg'>");
    s += htmlEscape(msg);
    s += F("</div>");
  }
  return s;
}

inline String htmlFooter() {
  String s;
  s += F("<footer>Garage Relay Controller &middot; ESP8266 + ATmega328P"
         "<br>Edit the footer text in webpages.h to add your own contact info."
         "</footer></body></html>");
  return s;
}

// ---------------------------------------------------------------------------
// Home page
// ---------------------------------------------------------------------------
inline String pageHome(bool mcuLinked) {
  String s = htmlHeader(F("Smart Home"));
  s += F("<h1>Smart Home</h1>");

  s += F("<div class='linkbar'><span style='color:");
  s += mcuLinked ? F("var(--green)") : F("var(--red)");
  s += F("'>&#9679;</span> Relay board link: <b>");
  s += mcuLinked ? F("Connected") : F("Not responding");
  s += F("</b></div>");

  s += F("<div class='box'><div class='row'>"
         "<form action='/all' method='POST' style='flex:1'>"
         "<input type='hidden' name='state' value='1'>"
         "<button class='btn btn-green' type='submit'>All ON</button></form>"
         "<form action='/all' method='POST' style='flex:1'>"
         "<input type='hidden' name='state' value='0'>"
         "<button class='btn btn-red' type='submit'>All OFF</button></form>"
         "</div></div>");

  for (uint8_t i = 0; i < cfg.relayCount; i++) {
    RelayConfig &r = cfg.relays[i];
    s += F("<div class='card'><h3>");
    s += htmlEscape(r.name);
    s += F("</h3><div class='status'>Status: <b>");
    s += r.momentary ? F("READY") : (r.state ? F("ON") : F("OFF"));
    s += F("</b> &middot; pin D");
    s += String(r.pin);
    s += F("</div>");

    if (r.momentary) {
      s += F("<form action='/relay' method='POST'>"
             "<input type='hidden' name='id' value='");
      s += String(r.id);
      s += F("'><input type='hidden' name='state' value='1'>"
             "<button class='btn btn-orange' type='submit'>Trigger</button></form>");
    } else {
      s += F("<div class='row'>"
             "<form action='/relay' method='POST' style='flex:1'>"
             "<input type='hidden' name='id' value='");
      s += String(r.id);
      s += F("'><input type='hidden' name='state' value='1'>"
             "<button class='btn btn-green' type='submit'>ON</button></form>"
             "<form action='/relay' method='POST' style='flex:1'>"
             "<input type='hidden' name='id' value='");
      s += String(r.id);
      s += F("'><input type='hidden' name='state' value='0'>"
             "<button class='btn btn-red' type='submit'>OFF</button></form>"
             "</div>");

      s += F("<label>Run for (minutes)</label>"
             "<form action='/relay/timer' method='POST' style='display:flex;gap:8px'>"
             "<input type='hidden' name='id' value='");
      s += String(r.id);
      s += F("'><input type='number' name='minutes' min='1' max='1440' value='30' "
             "style='flex:1;margin:0' required>"
             "<button class='btn btn-blue' type='submit' "
             "style='flex:0 0 auto;width:auto;margin:0;padding:12px 16px'>Run</button>"
             "</form>");
    }
    s += F("</div>");
  }

  s += F("<div class='box'><a href='/names'><button class='btn btn-purple'>"
         "Add / Edit Relays</button></a>"
         "<form action='/restart' method='POST' onsubmit=\"return confirm('Restart the device?');\">"
         "<button class='btn btn-grey' type='submit'>Restart</button></form></div>");

  s += htmlFooter();
  return s;
}

// ---------------------------------------------------------------------------
// WiFi settings page
// ---------------------------------------------------------------------------
inline String pageWiFiSettings(const String &msg, const String &status,
                                const String &curSsid, const String &curIp) {
  String s = htmlHeader(F("WiFi Settings"), msg);
  s += F("<h1>WiFi Settings</h1>");

  s += F("<div class='box'><h2>Current Connection</h2><p>Status: <b>");
  s += status;
  s += F("</b><br>Network: ");
  s += htmlEscape(curSsid);
  s += F("<br>IP: ");
  s += curIp;
  s += F("</p></div>");

  s += F("<div class='box box-green'><h2>Access Point (always on)</h2><p>Name: <b>");
  s += htmlEscape(cfg.apSsid);
  s += F("</b><br>IP: 192.168.4.1</p></div>");

  s += F("<div class='box'><h2>Connect to a network</h2>"
         "<form action='/wifi' method='POST'>"
         "<label>Network name (SSID)</label>"
         "<input type='text' name='ssid' maxlength='32' required>"
         "<label>Password</label>"
         "<input type='password' name='pass' maxlength='64'>"
         "<button class='btn btn-green' type='submit'>Save and connect (restarts)</button>"
         "</form></div>");

  s += F("<div class='box'><h2>Access point settings</h2>"
         "<form action='/apsettings' method='POST'>"
         "<label>AP name (SSID)</label><input type='text' name='ap_ssid' maxlength='32' value='");
  s += htmlEscape(cfg.apSsid);
  s += F("' required>"
         "<label>AP password (min 8 chars)</label>"
         "<input type='password' name='ap_pass' maxlength='64' placeholder='leave blank to keep current'>"
         "<label><input type='checkbox' name='ap_hidden' style='width:auto' ");
  s += cfg.apHidden ? F("checked") : F("");
  s += F("> Hide network name</label>"
         "<button class='btn btn-green' type='submit'>Save AP settings</button>"
         "</form></div>");

  s += F("<div class='box'>"
         "<a href='/currentip'><button class='btn btn-blue'>Current IP Settings</button></a>"
         "<form action='/clearwifi' method='POST' onsubmit=\"return confirm('Clear saved WiFi settings?');\">"
         "<button class='btn btn-red' type='submit'>Clear saved settings</button></form>"
         "<form action='/restart' method='POST' onsubmit=\"return confirm('Restart the device?');\">"
         "<button class='btn btn-grey' type='submit'>Restart device</button></form>"
         "</div>");

  s += htmlFooter();
  return s;
}

// ---------------------------------------------------------------------------
// Current IP settings page
// ---------------------------------------------------------------------------
inline String pageCurrentIP(const String &msg, const String &connType, const String &curIp,
                             const String &curGw, const String &curMask, const String &mac) {
  String s = htmlHeader(F("Current IP Settings"), msg);
  s += F("<h1>Current IP Settings</h1>");

  s += F("<div class='box'><h2>Current Status</h2><p>Connection type: <b>");
  s += connType;
  s += F("</b><br>IP: ");
  s += curIp;
  s += F("<br>Gateway: ");
  s += curGw;
  s += F("<br>Subnet: ");
  s += curMask;
  s += F("<br>MAC: ");
  s += mac;
  s += F("</p></div>");

  s += F("<div class='box'><h2>Static IP</h2>"
         "<form action='/currentip' method='POST'>"
         "<label><input type='checkbox' name='static_enabled' style='width:auto' ");
  s += cfg.staticIpEnabled ? F("checked") : F("");
  s += F("> Enable static IP</label>"
         "<label>IP address</label><input type='text' name='ip' value='");
  s += cfg.ip;
  s += F("'>"
         "<label>Gateway</label><input type='text' name='gateway' value='");
  s += cfg.gateway;
  s += F("'>"
         "<label>Subnet mask</label><input type='text' name='subnet' value='");
  s += cfg.subnet;
  s += F("'>"
         "<label>DNS</label><input type='text' name='dns' value='");
  s += cfg.dns;
  s += F("'>"
         "<button class='btn btn-green' type='submit'>Apply and save (restarts)</button>"
         "</form></div>");

  s += F("<div class='box'>"
         "<form action='/clearip' method='POST' onsubmit=\"return confirm('Clear saved IP settings?');\">"
         "<button class='btn btn-red' type='submit'>Clear saved IP settings</button></form>"
         "<a href='/wifi'><button class='btn btn-grey'>Back to WiFi Settings</button></a>"
         "</div>");

  s += htmlFooter();
  return s;
}

// ---------------------------------------------------------------------------
// MQTT settings page
// ---------------------------------------------------------------------------
inline String pageMQTTSettings(const String &msg) {
  String s = htmlHeader(F("MQTT Settings"), msg);
  s += F("<h1>MQTT Settings</h1>");

  s += F("<div class='box'><form action='/mqtt' method='POST'>"
         "<label>MQTT server (e.g. broker.hivemq.com)</label>"
         "<input type='text' name='server' value='");
  s += htmlEscape(cfg.mqttServer);
  s += F("'>"
         "<label>MQTT prefix (e.g. garage)</label>"
         "<input type='text' name='prefix' value='");
  s += htmlEscape(cfg.mqttPrefix);
  s += F("'>"
         "<label>Port</label>"
         "<input type='number' name='port' value='");
  s += String(cfg.mqttPort);
  s += F("'>"
         "<button class='btn btn-green' type='submit'>Save MQTT settings (restarts)</button>"
         "</form></div>");

  String p = cfg.mqttPrefix.length() ? cfg.mqttPrefix : String("prefix");
  s += F("<div class='info'><b>Topics format</b><br>"
         "Control: <span class='topic'>");
  s += p; s += F("/control/relay&lt;id&gt;</span><br>Status: <span class='topic'>");
  s += p; s += F("/status/relay&lt;id&gt;</span><br>Control all: <span class='topic'>");
  s += p; s += F("/control/all</span><br>Status all: <span class='topic'>");
  s += p; s += F("/status/all</span><br><br>Payload is <code>ON</code> or <code>OFF</code>."
         " &lt;id&gt; is the relay id shown on the Edit Relays page.</div>");

  s += htmlFooter();
  return s;
}

// ---------------------------------------------------------------------------
// Edit relays page (rename, reassign GPIO, add, delete)
// ---------------------------------------------------------------------------
inline String pageEditNames(const String &msg) {
  String s = htmlHeader(F("Edit Relays"), msg);
  s += F("<h1>Edit Relays</h1>");

  for (uint8_t i = 0; i < cfg.relayCount; i++) {
    RelayConfig &r = cfg.relays[i];
    s += F("<div class='card'><form action='/relay/update' method='POST'>"
           "<input type='hidden' name='id' value='");
    s += String(r.id);
    s += F("'><label>Name</label><input type='text' name='name' maxlength='24' value='");
    s += htmlEscape(r.name);
    s += F("' required>"
           "<label>GPIO (Arduino pin)</label><select name='pin'>");
    for (uint8_t p = 0; p < ALLOWED_PINS_COUNT; p++) {
      uint8_t pin = ALLOWED_PINS[p];
      // Pin is selectable if it's free, or if it's the one this relay already uses.
      bool takenByOther = false;
      for (uint8_t j = 0; j < cfg.relayCount; j++) {
        if (j != i && cfg.relays[j].pin == pin) { takenByOther = true; break; }
      }
      if (takenByOther) continue;
      s += F("<option value='");
      s += String(pin);
      s += F("'");
      if (pin == r.pin) s += F(" selected");
      s += F(">D");
      s += String(pin);
      s += F("</option>");
    }
    s += F("</select>"
           "<label><input type='checkbox' name='momentary' style='width:auto' ");
    s += r.momentary ? F("checked") : F("");
    s += F("> Momentary (pulse then auto-off, e.g. garage door relay)</label>"
           "<label>Pulse length (ms)</label><input type='number' name='pulse_ms' min='100' max='10000' value='");
    s += String(r.pulseMs);
    s += F("'>"
           "<button class='btn btn-green' type='submit'>Save</button>"
           "</form>"
           "<form action='/relay/delete' method='POST' onsubmit=\"return confirm('Delete this relay?');\">"
           "<input type='hidden' name='id' value='");
    s += String(r.id);
    s += F("'><button class='btn btn-red' type='submit'>Delete</button></form></div>");
  }

  if (cfg.relayCount < MAX_RELAYS) {
    s += F("<div class='card'><h3>Add relay</h3><form action='/relay/add' method='POST'>"
           "<label>Name</label><input type='text' name='name' maxlength='24' required>"
           "<label>GPIO (Arduino pin)</label><select name='pin'>");
    for (uint8_t p = 0; p < ALLOWED_PINS_COUNT; p++) {
      uint8_t pin = ALLOWED_PINS[p];
      bool taken = false;
      for (uint8_t j = 0; j < cfg.relayCount; j++) {
        if (cfg.relays[j].pin == pin) { taken = true; break; }
      }
      if (taken) continue;
      s += F("<option value='");
      s += String(pin);
      s += F("'>D");
      s += String(pin);
      s += F("</option>");
    }
    s += F("</select>"
           "<label><input type='checkbox' name='momentary' style='width:auto'> Momentary</label>"
           "<label>Pulse length (ms)</label><input type='number' name='pulse_ms' min='100' max='10000' value='800'>"
           "<button class='btn btn-purple' type='submit'>Add relay</button>"
           "</form></div>");
  } else {
    s += F("<div class='info'>Maximum of ");
    s += String(MAX_RELAYS);
    s += F(" relays reached (limited by free Arduino pins on this board).</div>");
  }

  s += htmlFooter();
  return s;
}

// ---------------------------------------------------------------------------
// Scheduling page (NTP time sync + per-relay ON/OFF times)
// ---------------------------------------------------------------------------
inline String pageSchedule(const String &msg, const String &timeStatus) {
  String s = htmlHeader(F("Scheduling"), msg);
  s += F("<h1>Scheduling</h1>");

  s += F("<div class='box'><h2>Time sync (NTP)</h2><p class='status'>");
  s += htmlEscape(timeStatus);
  s += F("</p><form action='/schedule/ntp' method='POST'>"
         "<label><input type='checkbox' name='ntp_enabled' style='width:auto' ");
  s += cfg.ntpEnabled ? F("checked") : F("");
  s += F("> Enable scheduling (syncs the clock over the internet)</label>"
         "<label>UTC offset in minutes (e.g. 120 for UTC+2)</label>"
         "<input type='number' name='utc_offset' value='");
  s += String(cfg.utcOffsetMinutes);
  s += F("'>"
         "<label>NTP server</label><input type='text' name='ntp_server' value='");
  s += htmlEscape(cfg.ntpServer);
  s += F("'>"
         "<button class='btn btn-green' type='submit'>Save</button></form></div>");

  if (cfg.relayCount == 0) {
    s += F("<div class='info'>Add a relay first on the Edit Relays page, then come back here"
           " to schedule it.</div>");
  }

  const char *dayLabels[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  const char *dayArgs[7]   = {"d0", "d1", "d2", "d3", "d4", "d5", "d6"};

  for (uint8_t i = 0; i < cfg.relayCount; i++) {
    RelayConfig &r = cfg.relays[i];
    s += F("<div class='card'><h3>");
    s += htmlEscape(r.name);
    s += F("</h3><form action='/schedule/relay' method='POST'>"
           "<input type='hidden' name='id' value='");
    s += String(r.id);
    s += F("'><label><input type='checkbox' name='on_enabled' style='width:auto' ");
    s += r.schedOnEnabled ? F("checked") : F("");
    s += F("> ");
    s += r.momentary ? F("Trigger at") : F("Turn ON at");
    s += F("</label><input type='time' name='on_time' value='");
    s += twoDigit(r.schedOnHour);
    s += F(":");
    s += twoDigit(r.schedOnMinute);
    s += F("'>");

    if (!r.momentary) {
      s += F("<label><input type='checkbox' name='off_enabled' style='width:auto' ");
      s += r.schedOffEnabled ? F("checked") : F("");
      s += F("> Turn OFF at</label><input type='time' name='off_time' value='");
      s += twoDigit(r.schedOffHour);
      s += F(":");
      s += twoDigit(r.schedOffMinute);
      s += F("'>");
    }

    s += F("<label>Repeat on</label><div class='row' style='flex-wrap:wrap'>");
    for (uint8_t d = 0; d < 7; d++) {
      s += F("<label class='daychip'><input type='checkbox' name='");
      s += dayArgs[d];
      s += F("' style='width:auto' ");
      if (r.schedDaysMask & (1 << d)) s += F("checked");
      s += F(">");
      s += dayLabels[d];
      s += F("</label>");
    }
    s += F("</div>"
           "<button class='btn btn-green' type='submit' style='margin-top:14px'>Save schedule</button>"
           "</form></div>");
  }

  s += htmlFooter();
  return s;
}

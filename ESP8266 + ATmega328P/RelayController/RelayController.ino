/*
  RelayController.ino
  ------------------------------------------------------------------------
  Runs on the ATmega328P side of the hybrid board. Owns the physical
  relay pins and executes commands sent by the ESP8266 (GarageRelayESP8266.ino)
  over the SoftwareSerial link on pins 10/11.

  Board: Arduino Uno (matches the ATmega328P on this hybrid board)
  Wiring (per README.md / the board's own documentation):
    ESP8266 RXD -> Arduino pin 11
    ESP8266 TXD -> Arduino pin 10
  DIP switches: set to the "Mega328+ESP8266" combination before running
  this normally (see README.md). Use the "upload to ATmega328" switch
  combination only while flashing this sketch.

  Protocol (newline-terminated ASCII lines):
    From the ESP8266:
      CFG:<n>:<pin1>,<pin2>,...      configure relay count + Arduino pins
      SET:<index>:<0|1>              set one relay OFF(0)/ON(1)
      PULSE:<index>:<ms>             turn relay ON, auto-OFF after <ms>
      ALL:<0|1>                      set every latching-mode relay at once
      PING                           -> replies PONG
    From this sketch, back to the ESP8266:
      READY                          sent right after boot, and repeated
                                      every 2s until a valid CFG arrives --
                                      lets the ESP8266 notice and re-sync
                                      if this chip ever resets on its own
                                      (brownout, EMI from relay switching,
                                      static) while the ESP8266 stays up.
      ACK:CFG / ACK:SET:.. / ACK:PULSE:.. / ACK:ALL:..   command accepted
      ERR:CFG                        CFG was rejected (bad/duplicate pin,
                                      count mismatch) -- previous relay
                                      map is kept unchanged
      PONG                           reply to PING
      STATE:<index>:0                sent whenever a pulse (momentary
                                      trigger OR an ad-hoc timer from the
                                      web UI) finishes and that relay
                                      auto-turns-off on its own, so the
                                      ESP8266 -- and anything watching over
                                      MQTT -- finds out even if it wasn't
                                      the one that started the countdown.
                                      Also intended for physical buttons if
                                      you add any later.

  Notes:
    - Pins 2-9, 12, 13 are free for relays. Pins 0/1 are reserved for the
      USB-serial (CH340) link, and 10/11 are used for this ESP8266 link
      -- do not reassign those four. This sketch checks every incoming
      pin number against ALLOWED_PINS below and refuses anything else,
      specifically so a corrupted/garbled CFG line can never turn pins
      10/11 into relay outputs and sever its own link to the ESP8266.
    - RELAY_ACTIVE_LOW assumes typical opto-isolated relay boards where a
      LOW signal energizes the relay. Flip it to false if yours is active-HIGH.
    - Safety: if your relays switch mains voltage (lights, garage motor,
      etc.), keep the low-voltage logic side and the mains side properly
      isolated, use an enclosure, and have the mains wiring done/checked
      by someone qualified. This sketch only concerns the low-voltage
      control signal.
*/

#include <SoftwareSerial.h>

#define MAX_RELAYS 8
#define RELAY_ACTIVE_LOW true   // set false if your relay board is active-HIGH
#define DEBUG_MODE false        // set true to print status on the hardware Serial (USB) pins

SoftwareSerial espSerial(10, 11); // RX, TX

// Keep in sync with ALLOWED_PINS in config.h (the ESP8266 sketch) -- this
// is the safety net on THIS chip in case a line ever arrives corrupted.
const uint8_t ALLOWED_PINS[] = {2, 3, 4, 5, 6, 7, 8, 9, 12, 13};
const uint8_t ALLOWED_PINS_COUNT = sizeof(ALLOWED_PINS) / sizeof(ALLOWED_PINS[0]);

bool isAllowedPin(uint8_t pin) {
  for (uint8_t i = 0; i < ALLOWED_PINS_COUNT; i++) {
    if (ALLOWED_PINS[i] == pin) return true;
  }
  return false;
}

uint8_t relayPins[MAX_RELAYS];
bool relayState[MAX_RELAYS];
uint8_t relayCount = 0;

bool pulseActive[MAX_RELAYS];
unsigned long pulseEndAt[MAX_RELAYS];

String lineBuf;
bool synced = false;            // true once a valid CFG has been applied
unsigned long lastReadyAt = 0;

void debugPrint(const String &s) {
  if (DEBUG_MODE) Serial.println(s);
}

// Strict integer parsing: returns -1 if s is empty or contains anything
// other than digits, instead of silently treating garbage as 0 like
// String::toInt() does. A single corrupted byte on the SoftwareSerial
// link should be dropped, not misread as a valid (wrong) command.
long parseUint(const String &s) {
  if (s.length() == 0) return -1;
  for (uint8_t i = 0; i < s.length(); i++) {
    if (!isDigit(s[i])) return -1;
  }
  return s.toInt();
}

void writeRelay(uint8_t idx, bool on) {
  if (idx >= relayCount) return;
  relayState[idx] = on;
  uint8_t level = on ? (RELAY_ACTIVE_LOW ? LOW : HIGH) : (RELAY_ACTIVE_LOW ? HIGH : LOW);
  digitalWrite(relayPins[idx], level);
}

// Validates the whole CFG line into a temporary array first, and only
// commits it if every pin is allowed and there are no duplicates --
// never leaves relayPins/relayCount half-updated on bad input.
void applyConfig(uint8_t n, const String &pinsCsv) {
  if (n > MAX_RELAYS) { espSerial.println("ERR:CFG"); return; }

  uint8_t newPins[MAX_RELAYS];
  uint8_t count = 0;
  int start = 0;
  bool ok = true;

  while (ok && count < n) {
    int comma = pinsCsv.indexOf(',', start);
    String tok = (comma == -1) ? pinsCsv.substring(start) : pinsCsv.substring(start, comma);
    long pinVal = parseUint(tok);

    if (pinVal < 0 || pinVal > 255 || !isAllowedPin((uint8_t)pinVal)) {
      ok = false;
      break;
    }
    uint8_t pin = (uint8_t)pinVal;
    for (uint8_t j = 0; j < count; j++) {
      if (newPins[j] == pin) { ok = false; break; } // duplicate pin in one CFG
    }
    if (!ok) break;

    newPins[count++] = pin;
    if (comma == -1) break;
    start = comma + 1;
  }

  if (!ok || count != n) {
    espSerial.println("ERR:CFG");
    return;
  }

  // Validated -- safe to commit. Release the previous pin map first.
  for (uint8_t i = 0; i < relayCount; i++) {
    digitalWrite(relayPins[i], RELAY_ACTIVE_LOW ? HIGH : LOW);
  }

  relayCount = n;
  for (uint8_t i = 0; i < relayCount; i++) {
    relayPins[i] = newPins[i];
    relayState[i] = false;
    pulseActive[i] = false;
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], RELAY_ACTIVE_LOW ? HIGH : LOW); // start OFF
  }

  synced = true;
  debugPrint("CFG applied, relayCount=" + String(relayCount));
  espSerial.println("ACK:CFG");
}

void handleLine(String line) {
  line.trim();
  if (line.length() == 0) return;

  if (line.startsWith("CFG:")) {
    int c1 = line.indexOf(':', 4);
    if (c1 < 0) return;
    long n = parseUint(line.substring(4, c1));
    if (n < 0 || n > MAX_RELAYS) { espSerial.println("ERR:CFG"); return; }
    applyConfig((uint8_t)n, line.substring(c1 + 1));

  } else if (line.startsWith("SET:")) {
    int c1 = line.indexOf(':', 4);
    if (c1 < 0) return;
    long idxVal = parseUint(line.substring(4, c1));
    long onVal  = parseUint(line.substring(c1 + 1));
    // Bounds-check BEFORE touching any array -- a stale or corrupted
    // index used to be written into pulseActive[] unchecked here, which
    // could corrupt memory on this 2KB-RAM chip and lock the board up.
    if (idxVal < 0 || onVal < 0 || idxVal >= relayCount) return;
    uint8_t idx = (uint8_t)idxVal;
    bool on = (onVal == 1);
    pulseActive[idx] = false; // a direct SET cancels any pending auto-off
    writeRelay(idx, on);
    espSerial.println("ACK:SET:" + String(idx) + ":" + String(on ? 1 : 0));

  } else if (line.startsWith("PULSE:")) {
    int c1 = line.indexOf(':', 6);
    if (c1 < 0) return;
    long idxVal = parseUint(line.substring(6, c1));
    long msVal  = parseUint(line.substring(c1 + 1));
    if (idxVal < 0 || msVal < 0 || idxVal >= relayCount) return;
    uint8_t idx = (uint8_t)idxVal;
    writeRelay(idx, true);
    pulseActive[idx] = true;
    pulseEndAt[idx] = millis() + (unsigned long)msVal;
    espSerial.println("ACK:PULSE:" + String(idx));

  } else if (line.startsWith("ALL:")) {
    long onVal = parseUint(line.substring(4));
    if (onVal < 0) return;
    bool on = (onVal == 1);
    for (uint8_t i = 0; i < relayCount; i++) {
      pulseActive[i] = false;
      writeRelay(i, on);
    }
    espSerial.println("ACK:ALL:" + String(on ? 1 : 0));

  } else if (line == "PING") {
    espSerial.println("PONG");
  }
}

void setup() {
  if (DEBUG_MODE) Serial.begin(9600);
  espSerial.begin(9600);
  for (uint8_t i = 0; i < MAX_RELAYS; i++) pulseActive[i] = false;
  espSerial.println("READY");
  lastReadyAt = millis();
}

void loop() {
  while (espSerial.available()) {
    char c = espSerial.read();
    if (c == '\n') {
      handleLine(lineBuf);
      lineBuf = "";
    } else if (c != '\r') {
      lineBuf += c;
      if (lineBuf.length() > 96) lineBuf = ""; // guard against line-noise/garbage
    }
  }

  // Keep announcing readiness until a valid CFG arrives. Covers both the
  // normal power-up race AND this chip resetting on its own later while
  // the ESP8266 stays running -- the old sketch had no way to recover
  // from that second case short of a full power cycle.
  if (!synced && millis() - lastReadyAt > 2000) {
    espSerial.println("READY");
    lastReadyAt = millis();
  }

  unsigned long now = millis();
  for (uint8_t i = 0; i < relayCount; i++) {
    if (pulseActive[i] && (long)(now - pulseEndAt[i]) >= 0) {
      writeRelay(i, false);
      pulseActive[i] = false;
      // Tell the ESP8266 this relay just auto-turned-off, whether the
      // pulse was a short momentary trigger or a longer ad-hoc timer
      // (GarageRelayESP8266.ino's /relay/timer). This is what lets the
      // Home page and MQTT status catch up on their own.
      espSerial.println("STATE:" + String(i) + ":0");
    }
  }
}

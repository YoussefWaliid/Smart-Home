/*
  ==========================================================
   Smart Relay Controller v3.0 - Arduino (receives commands
   from the ESP8266 over Serial)
  ==========================================================
  Command protocol (action + pin number):
    ADD:<pin>     -> sets the pin as OUTPUT and turns it off
                     (used when a new button is added)
    REMOVE:<pin>  -> releases the pin back to INPUT
                     (used when a button is deleted)
    ON:<pin>      -> turns the relay wired to this pin ON
    OFF:<pin>     -> turns the relay wired to this pin OFF

  If an ON or OFF command arrives for a pin that hasn't been
  set up yet, it is auto-configured on the fly, so nothing
  breaks if the two boards get out of sync after a restart.
  ==========================================================
*/

const int MAX_RELAYS = 12;

struct ManagedPin {
  int pin;
  bool active; // whether this pin is currently registered as a relay
};

ManagedPin managed[MAX_RELAYS];
int managedCount = 0;

String buffer = "";

// --------- Helper functions for managing the pin list ---------
int findManagedIndex(int pin) {
  for (int i = 0; i < managedCount; i++) {
    if (managed[i].pin == pin) return i;
  }
  return -1;
}

void setupPin(int pin) {
  int idx = findManagedIndex(pin);
  if (idx == -1) {
    if (managedCount >= MAX_RELAYS) {
      Serial.println("ERROR: MAX_RELAYS reached");
      return;
    }
    managed[managedCount].pin = pin;
    managed[managedCount].active = true;
    managedCount++;
  }
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  Serial.println("OK: ADD pin " + String(pin));
}

void removePin(int pin) {
  int idx = findManagedIndex(pin);
  if (idx != -1) {
    digitalWrite(pin, LOW);
    pinMode(pin, INPUT);
    // remove this entry from the array (shift the rest down)
    for (int i = idx; i < managedCount - 1; i++) {
      managed[i] = managed[i + 1];
    }
    managedCount--;
    Serial.println("OK: REMOVE pin " + String(pin));
  } else {
    Serial.println("IGNORED: pin " + String(pin) + " not managed");
  }
}

void setRelay(int pin, bool on) {
  if (findManagedIndex(pin) == -1) {
    // not registered yet? auto-configure it so the command isn't lost
    setupPin(pin);
  }
  digitalWrite(pin, on ? HIGH : LOW);
  Serial.println(String("OK: ") + (on ? "ON " : "OFF ") + String(pin));
}

// --------- Executes a received command ---------
void executeCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  int sep = cmd.indexOf(':');
  if (sep == -1) {
    Serial.println("IGNORED: Unknown command -> " + cmd);
    return;
  }

  String action = cmd.substring(0, sep);
  int pin = cmd.substring(sep + 1).toInt();

  if (action == "ADD") {
    setupPin(pin);
  } else if (action == "REMOVE") {
    removePin(pin);
  } else if (action == "ON") {
    setRelay(pin, true);
  } else if (action == "OFF") {
    setRelay(pin, false);
  } else {
    Serial.print("IGNORED: Unknown command -> ");
    Serial.println(cmd);
  }
}

void setup() {
  Serial.begin(9600);

  Serial.println("=================================");
  Serial.println("   SMART RELAY CONTROLLER v3.0");
  Serial.println("=================================");
  Serial.println("Ready to receive commands");
  Serial.println("Valid commands: ADD:<pin> , REMOVE:<pin> , ON:<pin> , OFF:<pin>");
  Serial.println("=================================");
}

void loop() {
  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '\n') {
      executeCommand(buffer);
      buffer = "";
    } else if (c != '\r') {
      buffer += c;
    }
  }
}

# Smart-Home
Smart Home Makes Life Easier

# Relay Controller — ESP8266 + ATmega328P

Two sketches for the ATmega328P + ESP8266 hybrid board:

- `GarageRelayESP8266/GarageRelayESP8266.ino` — flashes onto the **ESP8266**.
  Owns WiFi, the web control panel, MQTT, OTA updates, and the scheduler.
  Has no relays wired to it.
- `RelayController/RelayController.ino` — flashes onto the **ATmega328P**.
  Owns the actual relay pins and switches them on command.

The two talk to each other over the serial link described in the board's
own guide (the GitHub repo you linked), so upload/switch steps below match
that guide.

## What changed in this update

`RelayController.ino` was rejecting/losing commands rather than genuinely
being wired wrong — the pin usage already matched the board's own wiring
diagram. Three real bugs were found and fixed:

1. **Out-of-bounds array write.** The `SET:` handler wrote to
   `pulseActive[idx]` using an index straight from the serial line, before
   checking it was in range. On a chip with only 2KB of RAM, a single bad
   or corrupted index (very possible on a bit-banged `SoftwareSerial` link
   sitting next to a WiFi radio) could corrupt nearby memory and leave the
   board behaving erratically or not responding at all. Every command
   handler now validates its index against the current relay count
   *before* touching any array.
2. **No pin validation on the ATmega side.** `config.h`'s own comment said
   "keep this list in sync with RelayController.ino ALLOWED_PINS" — but
   that list didn't actually exist in the sketch. A bad `CFG` line could
   in principle reassign pins 10/11 (the ESP8266 link itself) as relay
   outputs, permanently killing the link until the board was re-flashed.
   `RelayController.ino` now carries its own `ALLOWED_PINS` list and
   rejects any `CFG` that touches a pin outside it (or repeats a pin),
   leaving the previous relay map untouched.
3. **No recovery if the ATmega resets on its own.** The ESP8266 only
   pushed the relay configuration once, right after its own boot. If the
   ATmega ever reset by itself — a brownout, EMI from switching a relay,
   static — while the ESP8266 kept running, it came back up with zero
   relays configured and stayed that way forever, with no way to tell.
   This is the change most likely to explain "it uploads fine but the two
   just don't talk": the ATmega now announces `READY` right after boot and
   keeps repeating it every 2s until it hears back, and the ESP8266 now
   pings the ATmega every few seconds and re-pushes the config the moment
   it hears `READY`. The **Home page now shows a live "Relay board link:
   Connected / Not responding" indicator**, so you can see the link status
   at a glance without a USB debug session.

If it's still not talking after re-flashing both chips, it's almost
certainly physical — see **Troubleshooting** at the bottom.

## 1. Wiring & DIP switches (from the board's guide)

| Purpose | Switches 1–7 |
|---|---|
| Upload sketch to ESP8266 | OFF OFF OFF OFF ON ON ON |
| Upload sketch to ATmega328P | OFF OFF ON ON OFF OFF OFF |
| **Normal run (Mega328 + ESP8266 linked)** | **ON ON OFF OFF OFF OFF OFF** |

Physical link used for normal run: `ESP8266 RXD → Arduino D11`, `ESP8266 TXD → Arduino D10`.

Relay pins available on the ATmega328P: **D2–D9, D12, D13** (10 pins). D0/D1
are reserved for USB upload, D10/D11 are reserved for the ESP8266 link —
both sketches keep those off-limits (RelayController.ino now actively
enforces this, see above).

## 2. Libraries (Arduino IDE → Library Manager)

- ESP8266 core (Boards Manager) — select board **"Generic ESP8266 Module"** for `GarageRelayESP8266.ino`
- **ArduinoJson** (v6.x) — used by the ESP8266 sketch
- **PubSubClient** (Nick O'Leary) — MQTT for the ESP8266 sketch
- LittleFS and ArduinoOTA — both bundled with the ESP8266 core, no separate install

`RelayController.ino` only needs `SoftwareSerial`, which is built in. Board: **Arduino Uno**.

## 3. Upload order

1. Switches → ATmega328P upload position → upload `RelayController.ino`.
2. Switches → ESP8266 upload position → upload `GarageRelayESP8266.ino`.
3. Switches → normal run position (`ON ON OFF OFF OFF OFF OFF`) → power up.
4. First boot has no saved WiFi network, so connect to the access point
   `ESP8266-Garage` (password `12345678` — **change this**, see below),
   browse to `192.168.4.1`, open **WiFi Settings**, and enter your home network.
5. Check the **Home** page — the link indicator near the top should read
   "Relay board link: Connected" within a few seconds.

## 4. Using the panel

- **Home** — link status, ON/OFF per relay, a **"Run for (minutes)"** ad-hoc
  timer per latching relay (pick a duration, hit Run, it auto-turns-off —
  the countdown itself runs on the ATmega, so it keeps going even if the
  ESP8266 reboots mid-timer), All ON/All OFF, Restart. Momentary relays
  don't get the timer control — they already have their own short Trigger
  pulse.
- **Edit Relays** — rename a relay, change which Arduino pin it drives, mark
  it **Momentary** (pulses for a set time then auto-turns-off — use this for
  a garage-door motor relay instead of a light), add a new relay (up to 8,
  limited by free pins), or delete one. Saving here pushes the new pin map
  to the ATmega328P immediately, no restart needed.
- **Scheduling** — turn on NTP time sync (needs internet access, so this
  only works while connected to your home WiFi, not on the standalone AP),
  set your UTC offset, then give any relay a daily ON time and/or OFF time
  and which days it repeats on. Momentary relays get a single "trigger at"
  time instead of ON/OFF. Schedules are checked once a minute.
- **WiFi Settings** — connect to a network, edit the always-on access point
  name/password, clear saved WiFi, jump to Current IP Settings.
- **Current IP Settings** — DHCP by default; switch on static IP and set
  IP/gateway/subnet/DNS.
- **MQTT Settings** — broker address, prefix, port. Topics:
  `prefix/control/relay<id>`, `prefix/status/relay<id>`,
  `prefix/control/all`, `prefix/status/all` (payload `ON`/`OFF`). The relay
  `id` is the number shown on the Edit Relays page (not the pin number).

## 5. OTA updates

Once the ESP8266 is on your WiFi, it advertises itself for over-the-air
updates under the hostname `garage-esp8266`. In the Arduino IDE, open
**Tools → Port** — after a few seconds the device should appear there as a
network port (this needs Bonjour/mDNS support on your PC, which is usually
already installed alongside the Arduino IDE on Windows, and is built in on
macOS/Linux). Select it and hit Upload as usual; you'll be prompted for a
password — enter the **current AP password** from WiFi Settings. Note that
changing the AP password immediately changes the OTA password too.

## 6. Serial protocol (ESP8266 → ATmega328P), for reference

```
CFG:<n>:<pin1>,<pin2>,...   configure relay count + pin map
SET:<index>:<0|1>           set one relay OFF/ON
PULSE:<index>:<ms>          turn ON, auto-OFF after <ms> (momentary relays)
ALL:<0|1>                   set every latching relay at once
PING -> PONG                health check, sent by the ESP8266 every ~4s
READY                       sent by the ATmega right after boot, and
                             repeated every ~2s until it gets a CFG back
STATE:<index>:0             sent by the ATmega whenever a pulse (momentary
                             trigger or a Home-page timer) ends on its own
ACK:... / ERR:CFG           sent by the ATmega after each command
```
9600 baud, matching the board's wiring. `RelayController.ino` also has a
`STATE:<index>:<0|1>` hook already read by the ESP8266 side — handy if you
later add physical push-buttons on the ATmega328P and want the web panel
and MQTT to reflect a local button press.

## 7. Before you power real loads

If any relay switches mains voltage (lights, a garage door motor, etc.),
keep the low-voltage control side and the mains side properly isolated, use
an enclosure, and have the mains wiring itself done or checked by someone
qualified — these sketches only handle the low-voltage control signal.

## 8. Troubleshooting: relay board link not responding

If the Home page keeps showing "Not responding" after re-flashing both
chips with the versions in this update, it's most likely physical, not
code:

- **DIP switches** — must be exactly `ON ON OFF OFF OFF OFF OFF` for
  normal run. It's easy to leave switch 7 stuck ON from the upload
  position by mistake.
- **Wiring** — double-check ESP8266 RXD → D11 and TXD → D10 aren't
  swapped; a swapped pair looks identical at a glance but means neither
  side ever hears the other.
- **Power up both together** — after changing switches, power-cycle the
  whole board rather than just resetting one side.
- **USB debug window** — the board's own "CH340 connect to ESP8266"
  switch position (5, 6 ON, 7 OFF) lets you open a serial monitor on the
  ESP8266 alone (disconnected from the ATmega in that position) if you
  need to see its own boot log.

If the link comes up but drops out repeatedly under normal use, it's
worth keeping the relay board's wiring away from the WiFi antenna area —
`SoftwareSerial` at 9600 baud is fairly tolerant, but tight routing next
to an active 2.4GHz radio is the classic source of occasional corrupted
bytes, which the new validation now safely discards instead of acting on.

## 9. Ideas for later (still on the table)

- **Basic auth on the web panel** — right now anyone on the WiFi/AP can hit
  it; a simple username/password would matter more once it's controlling a
  door.
- **mDNS** (`http://garage.local`) so you don't need to remember the IP.
- **Relay interlock** — block two chosen relays from being ON at the same
  time (useful if you ever drive a motor with two direction relays).
- **Home Assistant MQTT discovery** so relays show up automatically instead
  of manual YAML.
- **Config export/import** (download/upload the `config.json`) to make
  re-flashing or cloning a second unit painless.

Also worth changing before you rely on this: the default AP password
(`12345678`) in `config.h`, and the pulse length per momentary relay (800 ms
default — set it to whatever your garage motor's relay actually needs).

# PIXEL PRO: new wiring for LOLIN/WEMOS S2 Mini

The owner confirmed on 2026-09-21 that the controller is a LOLIN/WEMOS
ESP32-S2 Mini and that the keys will be wired to this new pin assignment.
This is a NEW wiring design, not a verified copy of an EezBotFun PCB.

Reference: https://www.wemos.cc/en/latest/_static/files/sch_s2_mini_v1.0.0.pdf
Zephyr board reference: boards/wemos/esp32s2_lolin_mini at
10ba6d0cb38bc3d258775d27982f707599320085.

| Control | GPIO | Test key |
|---|---:|---|
| K1 | 1 | A |
| K2 | 2 | B |
| K3 | 3 | C |
| K4 | 4 | D |
| K5 | 5 | E |
| K6 | 6 | F |
| K7 | 7 | G |
| K8 | 8 | H |
| Optional navigation left | 9 | NAV 1 |
| Optional navigation press | 10 | NAV 2 |
| Optional navigation right | 11 | NAV 3 |

Each normally-open contact connects its GPIO to GND. Internal pull-ups,
active-low; no matrix, no diode required. Leave unused navigation pins
unconnected. K1–K4 are the top row left-to-right, K5–K8 bottom row left-to-right.
Use GPIO labels printed on the board, not physical header pin numbers.

USB uses the existing USB-C connector: GPIO19 D-, GPIO20 D+.
Do not attach keys to these lines. GPIO0 is BOOT, EN is RESET, GPIO15
is the onboard LED. GPIO26–32 belong to flash/PSRAM and are not for keys.
The build uses 4 MB flash and no external PSRAM allocation.

Phase 2 reservations (NOT enabled or a completed LCD wiring design):
GPIO33–40 for an 8-bit LCD data bus; GPIO12,13,14,16,17 for LCD controls;
GPIO18 for RGB data, GPIO21 spare. Exact display controller, control
signals, backlight power and RGB level shifting must match the purchased
modules before wiring. Nothing in phase 1 drives these pins.

No encoder. No Bluetooth/Wi-Fi. Physical USB enumeration, contact order,
debounce and reconnect still require validation on the owner's hardware.

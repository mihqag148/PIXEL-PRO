# PIXEL PRO USB CDC protocol v1

Transport: native USB CDC ACM. Nominal baud: 115200.

Firmware 1.2.0 exposes HID keyboard + consumer control + CDC with a VIA-style configuration model.

## HELLO

HELLO / GET_INFO returns:

PIXELPRO|1|FW=1.2.0|MCU=ESP32S2|KEYS=8|LAYERS=4|MACROS=8|CAPS=HID,CDC,KEYMAP,LAYERS,MACRO|VID=303A|PID=80C2

## Keymap

There are four persistent layers, each with eight physical keys.

Read a layer:

GET_KEYMAP|0

Response:

KEYMAP|0|K:4:0,K:5:0,K:6:0,K:7:0,K:8:0,K:9:0,K:10:0,K:11:0

Write a complete layer:

SET_KEYMAP|0|K:4:0,K:5:0,K:6:0,K:7:0,K:8:0,K:9:0,K:10:0,K:11:0

Success:

OK|KEYMAP|0

Binding formats:

- K:<keyboard usage>:<modifier mask>
- C:<consumer usage>:0
- L:<layer>:<action>
- M:<macro slot>:0
- T:0:0 = transparent
- D:0:0 = disabled

Modifier mask:

- 1 = Left Ctrl
- 2 = Left Shift
- 4 = Left Alt
- 8 = Left GUI / Windows

Layer actions:

- 1 = MO(layer), momentary while held
- 2 = TG(layer), toggle layer
- 3 = TO(layer), switch base layer

Examples:

- Ctrl+C: K:6:1
- standalone Ctrl: K:0:1
- Play/Pause: C:205:0
- MO(1): L:1:1
- Macro 0: M:0:0

RESET_KEYMAP restores Layer 0 to A-H and Layers 1-3 to Transparent.

## Macros

Eight persistent ASCII macro slots are available. Each stores up to 80 printable ASCII characters.

Read:

GET_MACRO|0

Response uses hex-encoded ASCII:

MACRO|0|48656C6C6F

Write:

SET_MACRO|0|48656C6C6F

Success:

OK|MACRO|0

## Matrix test

Physical transitions are asynchronous:

KEY|1|DOWN|L=0
KEY|1|UP|L=0

GET_KEYS returns:

KEYS|00|L=0

GET_LAYER returns:

LAYER|ACTIVE=0|BASE=0|TOGGLE=0

## Other commands

- HELLO
- GET_INFO
- GET_KEYS
- GET_LAYER
- PING
- REBOOT

PING returns PONG|PIXELPRO.

The build uses the generic ESP32-S2 target with USB CDC On Boot disabled. CDC and HID descriptors are registered before USB.begin().

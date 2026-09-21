# PIXEL PRO USB CDC protocol v1

Firmware 1.3.0 keeps native USB HID + CDC and adds 20 keymap profiles.

## HELLO

HELLO / GET_INFO returns a line containing:

PIXELPRO|1|FW=1.3.0|MCU=ESP32S2|KEYS=8|PROFILES=20|LAYERS=4|MACROS=20|CAPS=HID,CDC,KEYMAP,LAYERS,HOST_MACRO|VID=303A|PID=80C2

## Keymap profiles

There are 20 persistent profiles. Each profile has 4 layers and 8 physical keys.

Read one layer:

GET_KEYMAP|<profile 0-19>|<layer 0-3>

Response:

KEYMAP|<profile>|<layer>|<8 bindings>

Write one layer:

SET_KEYMAP|<profile>|<layer>|<8 bindings>

Success:

OK|KEYMAP|<profile>|<layer>

Binding formats:

- K:<keyboard usage>:<modifier mask>
- C:<consumer usage>:0
- L:<layer>:<action>
- M:<macro slot 0-19>:0
- T:0:0 = transparent
- D:0:0 = disabled

Layer actions:

- 1 = MO(layer)
- 2 = TG(layer)
- 3 = TO(layer)

RESET_KEYMAP resets all 20 profiles. Layer 0 becomes A-H and layers 1-3 become Transparent.

## Active profile / layer

GET_PROFILE

SET_PROFILE|<profile 0-19>|<base layer 0-3>

Success:

OK|PROFILE|<profile>|<layer>

## Host macros

M1-M20 are host-driven macro slots. Firmware does not execute the macro body itself.

When a key mapped to M1-M20 is pressed, firmware emits:

MACRO|<1-20>|KEY=<1-8>|P=<profile>|L=<layer>

LumiPad receives this event and executes the stored Windows macro. This lets macros include keyboard combos, mouse actions, text, media, delays, running apps/files, and Lumi Action references.

## Matrix events

Physical key transitions:

KEY|<1-8>|DOWN|P=<profile>|L=<layer>
KEY|<1-8>|UP|P=<profile>|L=<layer>

GET_KEYS returns:

KEYS|<mask>|P=<profile>|L=<layer>

GET_LAYER returns active profile/layer state.

## Other commands

- HELLO
- GET_INFO
- GET_KEYS
- GET_LAYER
- GET_PROFILE
- PING
- REBOOT

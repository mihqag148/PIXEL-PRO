# PIXEL PRO USB CDC protocol v1

Firmware 1.3.3 keeps native USB HID + CDC, 20 keymap profiles, live memory telemetry and Lumi Action bindings, and adds the PIXEL PRO ILI9486 480×320 i8080 display/media path.

## HELLO

HELLO / GET_INFO returns a line containing:

PIXELPRO|1|FW=1.3.3|MCU=ESP32S2|KEYS=8|PROFILES=20|LAYERS=4|MACROS=20|ACTIONS=32|DISPLAY=ILI9486,480x320,i8080-8|CAPS=HID,CDC,KEYMAP,LAYERS,HOST_MACRO,HOST_ACTION,MEM,PANEL,SAVER,MEDIA|VID=303A|PID=80C2

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
- A:<Lumi Action 1-32>:0
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

## Lumi Action bindings

A key can be bound directly to Lumi Action 1-32 using:

A:<action id>:0

When that key is pressed, firmware emits:

ACTION|<1-32>|KEY=<1-8>|P=<profile>|L=<layer>

The Windows app looks up the matching Lumi Action and runs its stored script.

## Matrix events


Physical key transitions:

KEY|<1-8>|DOWN|P=<profile>|L=<layer>
KEY|<1-8>|UP|P=<profile>|L=<layer>

GET_KEYS returns:

KEYS|<mask>|P=<profile>|L=<layer>

GET_LAYER returns active profile/layer state.

## Panel information

Request:

PANEL

Response:

PANEL|ILI9486|60|0|60

Fields are panel name, PIXEL PRO refresh cap in Hz, legacy SPI-Hz field (0 for
i8080 parallel), and maximum media FPS.

## Screensaver media

PIXEL PRO accepts static 480×320 RGB565 images and animated 240×160 RGB332 frames.
Animated frames are expanded exactly 2× on the ILI9486 to fill the 480×320 3:2 panel.
The host caps animation timing at 60 FPS and stores at most 32 frames.

Begin an upload:

SAVBEGIN|<frames>|<width>|<height>|<RGB565 or RGB332>|<duration-ms CSV>

Then send sequential Base64 chunks:

SAVDATA|<frame index>|<byte offset>|<base64>

Firmware acknowledges each completed frame:

OK|SAVFRAME|<frame index>

Finish:

SAVEND

Successful completion:

OK|SAVER|READY

Other commands:

- SAVERSTATE -> SAVERSTATE|EMPTY / UPLOADING / READY
- SAVSHOW -> immediately show uploaded media
- SAVCLEAR -> clear uploaded media from PSRAM
- SAVSOURCE|MEDIA -> select uploaded media
- SAVDELAY|<seconds> -> inactivity delay, 0 disables auto screensaver

Uploaded PIXEL PRO media lives in PSRAM, so the Windows app may restore it after a
device reset. Static RGB565 is full resolution; GIF frames use the PSRAM-friendly
240×160 RGB332 format.

## Memory telemetry

Request:

MEM

or:

GET_MEMORY

Response:

MEM|<flash used>|<flash total>|<SRAM used>|<SRAM total>|<PSRAM used>|<PSRAM total>

All values are bytes. Flash used is the compiled sketch size and flash total is the physical flash-chip size. SRAM uses the Arduino heap size/free heap counters. PSRAM reports zero total on hardware where PSRAM is unavailable.

## Other commands


- HELLO
- GET_INFO
- GET_KEYS
- GET_LAYER
- GET_PROFILE
- PING
- REBOOT

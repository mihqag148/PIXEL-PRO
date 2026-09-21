# PIXEL PRO USB CDC protocol v1

Firmware 1.3.7 keeps native USB HID + CDC, 20 keymap profiles, live memory telemetry, Lumi Action bindings and the ILI9486 480×320 i8080 display. GIF files stay compressed and are decoded on-device; v1.3.5 also accepts arbitrary GIF canvas sizes up to 1024×1024 and scales them to the panel at render time.

## HELLO

HELLO / GET_INFO returns a line containing:

PIXELPRO|1|FW=1.3.7|MCU=ESP32S2|KEYS=8|PROFILES=20|LAYERS=4|MACROS=20|ACTIONS=32|DISPLAY=ILI9486,480x320,i8080-8|CAPS=HID,CDC,KEYMAP,LAYERS,HOST_MACRO,HOST_ACTION,MEM,PANEL,SAVER,MEDIA,DIRECT_GIF|VID=303A|PID=80C2

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

### Direct full-resolution GIF

PIXEL PRO accepts the original compressed GIF file. The app does not convert GIFs
to raw RGB332 frame arrays and does not resize them to 240×160.

Supported GIF logical canvas sizes are 1×1 through 1024×1024. The original compressed GIF is not converted into a reduced raw-frame asset.

- 480×320 displays 1:1.
- 320×480 remains portrait. The selected scale transform is applied without automatic rotation.
- Other sizes are scaled on-device.

Start a GIF upload:

SAVGIFBEGIN|<file bytes>|<width>|<height>|<FILL|FIT|STRETCH|TILE|CENTER|SPAN>

For backward compatibility, omitting the final scale token defaults to FILL.

Success:

OK|SAVGIFBEGIN

Send sequential Base64 chunks:

SAVGIFDATA|<byte offset>|<base64>

Each chunk is acknowledged with the next expected byte offset:

OK|SAVGIFDATA|<next offset>

Finish:

SAVGIFEND

Success:

OK|SAVER|READY

The original LZW-compressed GIF is persisted in LittleFS. AnimatedGIF decodes it
on-device and outputs RGB565 to the ILI9486. Scaling/cropping is performed while
rendering, so storage remains the original GIF file rather than raw resized frames.
Frame timing is kept, with a 17 ms minimum interval so playback is capped at 60 FPS.

### Static image / legacy compatibility

Static images use the existing full-resolution 480×320 RGB565 transport:

SAVBEGIN|<frames>|<width>|<height>|<RGB565 or RGB332>|<duration-ms CSV>
SAVDATA|<frame index>|<byte offset>|<base64>
SAVEND

The RGB332 path remains only for compatibility with older app builds.

Storage query:

SAVERINFO

Response:

SAVERINFO|TOTAL=<bytes>|USED=<bytes>|FREE=<bytes>|FLASH=<bytes>

When a GIF is larger than the available LittleFS media area, SAVGIFBEGIN now returns:

ERR|NO_SPACE|NEED=<bytes>|FREE=<bytes>|TOTAL=<bytes>

This lets Lumi Macropad distinguish a storage-capacity problem from a firmware mismatch.

Other commands:

- SAVERSTATE -> SAVERSTATE|EMPTY / UPLOADING / READY
- SAVSHOW -> immediately show uploaded media
- SAVCLEAR -> clear uploaded media and the persisted GIF
- SAVSOURCE|MEDIA -> select uploaded media
- SAVDELAY|<seconds> -> inactivity delay, 0 disables auto screensaver

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

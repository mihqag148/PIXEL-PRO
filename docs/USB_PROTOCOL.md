# PIXEL PRO USB CDC protocol v1

Firmware 1.5.1 keeps native USB HID + CDC, 20 keymap profiles, live memory telemetry, Lumi Action bindings and the ILI9486 480×320 i8080 display. GIF files stay compressed and are decoded on-device; v1.3.5 also accepts arbitrary GIF canvas sizes up to 1024×1024 and scales them to the panel at render time.

## HELLO

HELLO / GET_INFO returns a line containing:

PIXELPRO|1|FW=1.5.1|MCU=ESP32S2|KEYS=8|PROFILES=20|LAYERS=4|MACROS=20|ACTIONS=32|DISPLAY=ILI9486,480x320,i8080-8|CAPS=HID,CDC,KEYMAP,LAYERS,HOST_MACRO,HOST_ACTION,MEM,PANEL,SAVER,MEDIA,DIRECT_GIF,DIRECT_JPEG,PXQ,RLE,DELTA,RGB_PER_KEY,RGB_EFFECTS,ROM_BOOT|VID=303A|PID=80C2

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

PIXEL PRO accepts a compressed GIF file. Lumi Macropad 1.20.10 can re-encode
oversized/high-FPS GIFs before upload to reduce LittleFS usage; it never stores
raw 480×320 frame dumps.

Supported GIF logical canvas sizes are 1×1 through 1024×1024. Small GIFs default to CENTER mode and are never enlarged unless the user explicitly selects another scale mode.

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

The uploaded LZW-compressed GIF payload is persisted in LittleFS. AnimatedGIF decodes it
on-device and outputs RGB565 to the ILI9486. Scaling/cropping is performed while
rendering. Storage remains a compressed GIF payload rather than raw framebuffer
frames. Firmware playback keeps a 17 ms minimum interval (up to 60 FPS); Lumi
Macropad may merge source frames faster than 25 FPS when it creates an optimized
storage payload.

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


## Native USB reboot safety

Firmware 1.5.0 disables the Arduino USB CDC DTR/RTS reboot hook during normal
Lumi Macropad communication. Opening, closing, or relaunching the Windows app
therefore cannot request ROM bootloader mode. Firmware update continues to use
the explicit ROM BOOT/esptool flow.


## Explicit ROM bootloader entry

Firmware 1.5.0 keeps normal CDC reboot handling disabled. Lumi Macropad must first send:

ARM_BOOTLOADER

Success:

OK|BOOTLOADER_ARMED

The firmware then enables the Arduino-ESP32 DTR/RTS ROM-boot sequence for five seconds only. Lumi Macropad emits the intentional 01 -> 11 -> 10 -> 00 DTR/RTS sequence during Update Firmware. Normal app reconnects and app updates never arm this path.


## PIXEL PRO media v1.4.0

Firmware 1.4.0 introduced the approximately 60 Hz media path and an initial 1 MiB app-side GIF optimization target. Later direct-GIF builds made upload sizing capacity-based. Static images are prepared as high-quality JPEG on the PC and decoded directly on-device without upscaling.

JPEG upload:
- `SAVJPGBEGIN|bytes|width|height`
- `SAVJPGDATA|offset|base64`
- `SAVJPGEND`

## Per-key RGB

RGB is stored per keymap profile in logical key order K1..K8.

- `GET_RGB_PROFILE|profile`
- `RGB_PROFILE_SET|profile|RRGGBB,...(8)`
- `RGB_KEY|profile|key|r|g|b`
- `RGB_ALL|profile|r|g|b`
- `RGB_ENABLE|0|1`
- `RGB_BRIGHTNESS|0..100`

Physical LED mapping: 1→K1, 2→K2, 3→K3, 4→K4, 5→K8, 6→K7, 7→K6, 8→K5.


## PIXEL PRO RGB effects v1.4.1

PIXEL PRO now supports per-profile RGB mode in addition to per-key static colors.

- `RGB_EFFECT|profile|effect`
  - `0` Rainbow
  - `1` Purple Ping-Pong
  - `2` Orange Blink
  - `3` Static per-key colors
- `RGB_SPEED|10..100`

The active effect is stored per keymap profile. Speed is global.

## GIF sizing v1.4.1

The 1 MiB value in Lumi Macropad is a soft optimization target, not a hard firmware rejection limit. Firmware accepts a larger direct GIF when it fits the LittleFS media partition. Uploaded and persisted GIFs always use Center/no-upscale mode: small GIFs are never enlarged, while oversized GIFs are reduced only as needed to fit the 480×320 display.


## PIXEL packed animation (PXQ) v1.5.0 / v1.5.1

PXQ was introduced in firmware 1.5.0. Firmware 1.5.1 raises the packed-media ceiling to 1100 KiB while keeping the decoder format backward compatible.

PIXEL PRO GIF media is converted on the PC into the PIXEL-specific `PXQ1` animation format. It follows the same compression ideas as QMK Quantum Painter without embedding QGF itself:

- delta frames: only changed row spans are stored after the first frame
- RLE packets inside each changed span
- decoder compatibility remains RGB888, RGB565, palette 256, palette 16, palette 4, palette 2
- Lumi Macropad 1.20.x and newer generates new PIXEL media at RGB888, RGB565, or palette 256 only
- logical display canvas is always 480×320
- new app-generated storage canvas is 480×320 or 360×240; 240×160 remains accepted only for older app payloads
- packed payload may be up to 1100 KiB

Upload commands:
- `SAVPXBEGIN|bytes`
- `SAVPXDATA|offset|base64`
- `SAVPXEND`

The app chooses the highest-quality configuration that fits the 1100 KiB ceiling while preserving at least 360×240 and palette 256. Fill, Fit, Stretch, Tile, Center and Span are composed into the logical 480×320 canvas before packing. PIXEL GIF duration defaults to 10 seconds in the companion app.

## PIXEL RGB effects v1.5.0

Effect ids:
- 0 Rainbow
- 1 Purple Ping-Pong
- 2 Orange Blink
- 3 Static per-key
- 4 Fade
- 5 Chase
- 6 Breathe
- 7 Color Shift
- 8 Rain
- 9 Wave

RGB colors and effect are stored per keymap profile. Effect speed remains global.

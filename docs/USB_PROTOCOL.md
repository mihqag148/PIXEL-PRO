# PIXEL PRO USB CDC protocol v1

Firmware 1.10.1 keeps native USB HID + CDC, the verified HX8357-B 3.5-inch 480×320 i8080 display using the MCUFRIEND-compatible init path, 2×4 key matrix, horizontal roller, resistive touch, SPI microSD, and a three-port PCA9546A magnetic module bus.
## HELLO

HELLO / GET_INFO returns a line containing:

PIXELPRO|1|FW=1.10.1|MCU=ESP32S2|KEYS=8|PROFILES=20|LAYERS=4|MACROS=20|ACTIONS=32|DISPLAY=HX8357B-MCUFRIEND,480x320,i8080-8|CAPS=HID,CDC,KEYMAP,LAYERS,HOST_MACRO,HOST_ACTION,MEM,PANEL,SAVER,MEDIA,DIRECT_GIF,DIRECT_JPEG,PXQ,RLE,DELTA,RGB_PER_KEY,RGB_EFFECTS,MAIN_MENU,MAIN_MENU_ICONS,PCMON,MATRIX_2X4,ENCODER,ROLLER_EVQWGD001,TOUCH_RESISTIVE,SD_SPI,MODULE_I2C,PCA9546A,3PORT,ROM_BOOT|VID=303A|PID=80C2

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

## Display controller compatibility

Firmware 1.10.1 keeps the measured panel ID **0x8357 (HX8357-B)**. The ID
diagnostic read register 0xBF as `00 01 62 83 57 FF`. Instead of applying the
native Arduino_HX8357B power/timing table, firmware now mirrors MCUFRIEND_kbv's
actual 0x8357 behavior: software reset, display off, RGB565 pixel format, sleep
out, display on, rotation, and REV_SCREEN polarity. This avoids overwriting
panel-specific timing/voltage defaults on this shield revision.

The shop sketch supplied for this panel uses `MCUFRIEND_kbv`, calls
`tft.readID()`, and passes the returned value to `tft.begin(ID)`. Its
`//ID=0x9341` text is attached to the generic touch calibration constants and
is not a hard-coded panel driver selection.

Display wiring remains unchanged:

- LCD_RST -> EN
- LCD_RD -> 3V3
- LCD_WR -> D13
- LCD_RS/DC -> D14
- LCD_CS -> D16
- LCD_D0..LCD_D7 -> D33..D40

## Physical input subsystem

Firmware 1.9.6 scans the eight keys as a 2 × 4 diode matrix. Logical key event
messages remain unchanged, so existing LumiPad keymap handling stays compatible.

The EVQWGD001-style horizontal roller is handled directly by firmware. It is
electrically a quadrature encoder plus a momentary push switch:

- roll clockwise -> USB Consumer Volume Increment
- roll counter-clockwise -> USB Consumer Volume Decrement
- press -> USB Consumer Mute

Diagnostic CDC events are also emitted:

- `ENCODER|CW`
- `ENCODER|CCW`
- `ENCODER|PRESS`

### Resistive touch

The 4-wire touch panel shares LCD_WR, LCD_RS, LCD_D6 and LCD_D7. Firmware
deselects the LCD before every touch sample and restores the 8080 bus afterward.

Firmware 1.10.1 reproduces the supplied shop sketch's TouchScreen.h acquisition:
XP=D39, XM=D14, YP=D13, YM=D40, 300-ohm X plate and pressure 200..1000.
Calibration is applied on the correct raw axes: tp.x uses 139..942, tp.y uses
136..907, then landscape mapping swaps X/Y and inverts both axes.

Touch diagnostic events:

- `TOUCH|DOWN|X=<x>|Y=<y>|RAWX=<raw>|RAWY=<raw>|P=<pressure>|SLOT=<0-8>`
- `TOUCH|UP`

A Main Menu touch on a configured slot emits the same Lumi Action event family
used by a physical action key:

`ACTION|<1-32>|KEY=0|P=<profile>|L=<layer>|TOUCH=<1-8>`

The first touch while the screensaver is active only wakes the display and is not
dispatched as an action.

Calibration commands:

- `GET_TOUCH_CAL`
- `SET_TOUCH_CAL|<xMin>|<xMax>|<yMin>|<yMax>|<flags>`
- `RESET_TOUCH_CAL`

`GET_TOUCH_CAL` returns:

`TOUCH_CAL|<xMin>|<xMax>|<yMin>|<yMax>|<flags>`

Flags: bit 0 swaps X/Y, bit 1 inverts X, bit 2 inverts Y.

## microSD

Firmware 1.9.3 mounts the TFT shield microSD slot on a dedicated SPI bus mapping:

- SD_SCK -> D9
- SD_DO / MISO -> D10
- SD_DI / MOSI -> D11
- SD_SS / CS -> D12

The bus starts at 20 MHz. LCD_RD is no longer assigned to D12; it must be tied
HIGH to 3V3 because the parallel display path is write-only.

Commands:

- `SDINFO` or `GET_SD`
- `SDREMOUNT`
- `SDTEST`

No card:

`SDINFO|ABSENT`

Mounted card:

`SDINFO|READY|TYPE=<type>|CARD=<bytes>|TOTAL=<bytes>|USED=<bytes>|HZ=20000000`

`SDTEST` performs a temporary write/read/verify/delete cycle and returns
`SDTEST|OK` or `SDTEST|FAIL`.

Existing screensaver and Main Menu upload commands continue to use LittleFS in
1.9.3; SD support is intentionally additive and does not change existing media
persistence semantics.

## Magnetic expansion modules

Firmware 1.9.6 uses:

- D18 = module SDA
- D17 = module SCL
- PCA9546A address 0x70
- physical PORT 1/2/3 = PCA9546A channel 0/1/2
- module address 0x42 on every isolated channel
- 400 kHz I2C

LCD_RST is no longer on D17; wire LCD_RST to S2 Mini EN. RGB data moves from D18
to D15.

Connection events:

`MODULE|PORT=<1-3>|CONNECTED`

`MODULE|PORT=<1-3>|DISCONNECTED`

New valid module frame:

`MODULE_DATA|PORT=<1-3>|TYPE=<type>|ID=<id>|SEQ=<seq>|BTN=<hex16>|E1=<delta>|E2=<delta>|S1=<0-65535>|S2=<0-65535>|FLAGS=<hex8>`

Commands:

- `GET_MODULES`
- `MODULE_SCAN`
- `MODULE_TX|<port 1-3>|<hex payload>`

The fixed 16-byte module frame and CRC definition are documented in
`docs/MODULE_BUS.md`.

## Panel information

Request:

PANEL

Response:

PANEL|HX8357B-MCUFRIEND|60|0|60

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
on-device and outputs RGB565 to the 480x320 parallel TFT. Scaling/cropping is performed while
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
- SAVCLEAR -> clear user-uploaded media and immediately fall back to the firmware default screensaver
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

PXQ was introduced in firmware 1.5.0. Firmware 1.5.3 raises the packed-media ceiling to 2 MiB while keeping the decoder format backward compatible.

PIXEL PRO GIF media is converted on the PC into the PIXEL-specific `PXQ1` animation format. It follows the same compression ideas as QMK Quantum Painter without embedding QGF itself:

- delta frames: only changed row spans are stored after the first frame
- RLE packets inside each changed span
- decoder compatibility remains RGB888, RGB565, palette 256, palette 16, palette 4, palette 2
- Lumi Macropad 1.20.19 and newer generates new PIXEL media at RGB888 or RGB565 only; RGB565 is the color-quality floor
- logical display canvas is always 480×320
- new app-generated storage canvas is 480×320 or 360×240; 240×160 remains accepted only for older app payloads
- packed payload may be up to 2 MiB

Upload commands:
- `SAVPXBEGIN|bytes`
- `SAVPXDATA|offset|base64`
- `SAVPXEND`

The app chooses the highest-quality configuration that fits the 2 MiB ceiling while preserving at least 360×240 and RGB565. Fill, Fit, Stretch, Tile, Center and Span are composed into the logical 480×320 canvas before packing. PIXEL GIF duration defaults to 10 seconds in the companion app.

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


## Flash media layout v1.5.2

Firmware 1.5.3 uses a custom 4 MB flash partition table optimized for persistent media:

- 20 KiB NVS for Preferences/keymap settings
- 1.5 MiB factory application partition
- 0x270000 bytes (2.44 MiB) flash filesystem partition for LittleFS media

GIF/PXQ/JPEG payloads are persisted in flash LittleFS (for example `/screensaver.pxq`) and survive reset/power loss. PSRAM is never the persistent media store. It is used only as transient rendering/decode workspace. Direct GIF playback uses the 480×320 RGB565 PSRAM framebuffer when PSRAM is available; packed PXQ playback also allocates its line decode buffer from PSRAM with an internal-SRAM fallback.

`MEM` flash usage remains the compiled sketch size versus physical flash-chip size. `SAVERINFO` reports the LittleFS media partition total/used/free values. These are intentionally different measurements.


### PIXEL packed animation sizing v1.5.2

The packed PXQ upload ceiling is 2 MiB. The 2.44 MiB LittleFS media partition leaves headroom for filesystem metadata and upload overhead. Lumi Macropad chooses candidates in this quality order: color depth first (RGB888 before RGB565), then higher FPS, then higher storage resolution (480×320 before 360×240 at the same FPS). RGB565 and 360×240 remain the hard quality floors for newly generated media.


## PXQ minimum FPS v1.5.3

Packed PXQ media is accepted only when its declared encoder FPS is between 15 and 60. This makes 15 FPS a firmware-enforced floor for PIXEL PRO packed animation, matching Lumi Macropad 1.20.21. If a source cannot fit the 2 MiB budget at RGB565 / 15 FPS / 360×240, the app must reject the conversion instead of creating a lower-FPS PXQ.


## Main menu artwork protocol (firmware 1.6.0)

PIXEL PRO exposes a static 4-page main menu. Each page maps to one keyboard
layer and contains 12 visual action slots in a 4×3 layout.

Background:
- `MENUBGBEGIN|<bytes>` starts a JPEG upload. The image must be exactly
  480×320 and at most 96 KiB.
- `MENUBGDATA|<offset>|<base64>` streams the JPEG.
- `MENUBGEND` validates and stores it as `/menu_bg.jpg`.
- `MENUBGCLEAR` removes the background.

Icons:
- `MENUICONBEGIN|<page>|<slot>|3200` starts one 40×40 RGB565 icon upload.
- `MENUICONDATA|<offset>|<base64>` streams icon bytes.
- `MENUICONEND` stores the icon.
- `MENUICONCLEAR|<page>|<slot>` removes one icon.

Page mapping:
- `MENUCFG|<page>|<layer>|<a1>,...,<a12>` stores the layer and action IDs.
  Page is 0..3, layer is 0..3, action IDs are 0..32 (0 means empty).
- `GET_MENUCFG|<page>` returns the persisted mapping.
- `MENUSHOW` exits the screensaver and renders the current layer's menu.

Main-menu backgrounds and icons are static images only. The Windows app rejects
GIF input for these assets and bakes the requested background brightness into
the uploaded JPEG.

## Flash usage reporting (firmware 1.6.0)

The `MEM` response now reports Flash used as compiled sketch bytes plus the
current LittleFS used bytes, capped at the physical flash size. This makes the
app's Flash gauge reflect uploaded screensaver and main-menu media instead of
showing only sketch size.

Replacement screensaver uploads reclaim the previous screensaver before the
firmware checks free space. Therefore a full media partition can replace its
current GIF/PXQ/JPEG without requiring a manual clear first.


## Main Menu UX v1.6.1

Firmware 1.6.1 keeps the same 4-page / 12-slot Main Menu storage, but each
slot now also stores a short action label (up to 16 ASCII characters).

- If a slot has an icon, the icon is rendered icon-only and scaled 2× on the
  LCD to fill the action frame more naturally; no action text is drawn below it.
- If a slot has no icon, the stored Lumi Action name is rendered in the frame.
- `MENUCFG` now carries both the 12 action IDs and 12 labels:
  `MENUCFG|<page>|<layer>|<actionsCSV>|<labelsCSV>`.
- Companion app 1.21.1 integrates Main Menu editing into Home and adds
  background opacity in addition to brightness. Opacity/brightness are baked
  into the uploaded static 480×320 JPEG, so no extra runtime RAM is required.


## Profile Main Menu protocol (firmware 1.7.0)

Firmware 1.7.0 binds Main Menu state to the active keymap profile (0..19)
instead of the active layer. Layer changes no longer select or redraw a
different menu.

- `MENUCFG|<profile>|<actionsCSV>|<labelsCSV>` stores 12 Lumi Action IDs and
  12 short labels for one keymap profile.
- `GET_MENUCFG|<profile>` returns that profile's menu configuration.
- `
Read the background that is actually active on the device for one profile:

`MENUBGSTATE|<profile 0-19>`

Response when the firmware factory dune is active:

`MENUBGSTATE|PROFILE=<profile>|STATE=FACTORY|ASSET=DUNE_RED_ORANGE|BYTES=18055|W=480|H=320`

Response when a user JPEG is stored on the device:

`MENUBGSTATE|PROFILE=<profile>|STATE=CUSTOM|ASSET=USER_JPEG|BYTES=<bytes>|W=480|H=320`

MENUBGBEGIN|<profile>|<bytes>`, `MENUBGDATA`, and `MENUBGEND` upload
  one 480x320 JPEG background for a profile.
- `MENUBGCLEAR|<profile>` removes that profile's background.
- `MENUICONBEGIN|<profile>|<slot>|3200`, `MENUICONDATA`, and
  `MENUICONEND` upload one 40x40 RGB565 source icon.
- `MENUICONCLEAR|<profile>|<slot>` removes one icon.
- `MENUSHOW` renders the active profile's menu immediately.

The companion app 1.22.0 prepares static backgrounds with per-profile Blur,
Opacity, and Fill/Fit/Stretch settings before upload. These effects therefore
consume no extra runtime framebuffer RAM. App/EXE assignment reuses the Lumi
Action Run mechanism and can extract the associated EXE icon. Firmware scales
the 40x40 icon source to nearly fill its action frame while preserving the
icon-only behavior.


## 8-key HQ Main Menu + status strip (firmware 1.8.0)

Firmware 1.8.0 changes the PIXEL PRO Main Menu to match the eight physical
keys and the eezBotFun-style screen layout used as the visual reference:

- Main Menu is 2 rows x 4 action icons (8 slots total), still stored per
  Keymap Profile 01..20 and independent of keyboard layers.
- The former third icon row is replaced by a fixed bottom status strip showing
  active profile, PC date/time, CPU load/temperature, and GPU load/temperature.
- The companion app sends the status strip telemetry with:
  `PCMON|cpuLoad|cpuTemp|gpuLoad|gpuTemp|month|day|hour|minute`.
  Unknown sensor values are sent as -1. `PCCLEAR` clears the telemetry.
- `PCMON` is advertised in the firmware capability list.

High-quality icons:

- `MENUICONBEGIN|<profile>|<slot>|<bytes>` now accepts a JPEG icon for
  slot 0..7, up to 24 KiB.
- Icon dimensions must be exactly 96x96. Firmware validates the JPEG and
  decodes it directly at native size, removing the old 40x40-to-large-frame
  upscaling blur.
- Companion app 1.23.0 extracts a high-resolution EXE icon when available,
  trims transparent padding, preserves aspect ratio, and normalizes both
  EXE icons and user-selected custom images through the same 96x96 pipeline.
- Changing the Main Menu action dropdown now replaces the complete visual
  assignment too: a Run/EXE action receives that app's icon; a non-app action
  clears the previous app icon and falls back to the current action name.
- Existing firmware 1.7.0 12-slot profile mappings migrate by preserving
  slots 1..8 and dropping the former third row.

Background behavior remains per keymap profile with Blur, Opacity, and
Fill/Fit/Stretch preparation in Lumi Macropad before the 480x320 JPEG upload.

### v1.8.2 visual layout

PIXEL PRO Main Menu uses two rows of four app/action icons with a small action-name label under each icon and a four-part bottom status strip (profile, date/time, CPU, GPU), matching the eezBotFun-style reference layout. Slot card outlines were removed so the wallpaper remains visible.


## Firmware default visuals (firmware 1.9.1)

PIXEL PRO 1.9.1 keeps the factory Main Menu wallpaper and animated screensaver
available even when the user has never uploaded media.

- The factory visual now follows the user's blue/cyan/teal reference: a bright
  aqua sky, translucent-looking turquoise layers, smooth cyan ribbons and a
  deep navy foreground. The old pink/orange orb design is removed.
- The fallback is rendered as full-width RGB565 scanlines with per-pixel
  gradients and smooth wave boundaries. It remains compiled into firmware,
  consumes no LittleFS media space, and cannot be deleted through USB media
  commands.
- Main Menu uses a static frame from the same visual. The eight profile/action
  icons and bottom status strip are still drawn normally on top.
- A valid user Main Menu JPEG remains higher priority. `MENUBGCLEAR|<profile>`
  removes only that user override and immediately reveals the aqua factory
  wallpaper.
- A valid user GIF/JPEG/PXQ screensaver remains higher priority. If none is
  stored, or after `SAVCLEAR`, the aqua factory animation is active.
- The factory animation runs at 20 FPS. Its layers drift independently for
  smoother motion than the previous 12 FPS fallback.
- `SAVERSTATE` reports `READY` for the factory screensaver.
- `SAVMEDIA` reports the fallback as `STATE=READY|KIND=DEFAULT` with a
  480×320 logical size and 20 FPS.
- Because the fallback is inside the application image rather than LittleFS,
  formatting or clearing LittleFS cannot remove it.


## Factory dune screensaver (firmware 1.9.7)

PIXEL PRO 1.9.7 keeps the compiled red/orange dune JPEG as the factory Main
Menu background and replaces the previous aqua factory screensaver with a
matching animated red/orange dune scene.

- The factory screensaver is firmware-resident and consumes no LittleFS media
  space.
- It renders at 480x320 and 20 FPS using smooth RGB565 scanlines.
- User GIF/JPEG/PXQ media still has priority over the factory animation.
- After `SAVCLEAR`, or when no user screensaver exists, the factory dune
  animation becomes active automatically.
- `SAVMEDIA` reports the factory saver as:
  `STATE=READY|KIND=DEFAULT|ASSET=DUNE_RED_ORANGE_ANIM|...|W=480|H=320|FPS=20`.
- The factory animation cannot be deleted by media-management commands.


### Retry-safe media writes (firmware 1.10.7)

The host may repeat the immediately previous DATA chunk if its USB CDC ACK was
lost. Firmware accepts an exact replay where
`offset + decodedLength == currentReceivedBytes` without writing the payload
twice. This applies to Main Menu background/icon data, saver thumbnails,
direct GIF/JPEG data, and the existing packed PXQ path.

`SAVMEDIA` now includes `FORMAT=<n>` and `DEFAULT=0|1` so LumiPad can
distinguish firmware fallback artwork from a stored custom screensaver.


### Device-owned Main Menu persistence (firmware 1.10.8)

PIXEL PRO is the authoritative store for the active Main Menu:

- action IDs and labels are persisted in NVS and `MENUCFG` returns
  `ERR|MENUCFG_SAVE` if the NVS write is not confirmed;
- the active profile and base layer are persisted and restored after reboot;
- Main Menu background/icon uploads are transactional: the current final asset
  is kept until the new temporary file is complete and validated;
- final replacement uses a `.bak` rollback file, and boot/FSREPAIR restores a
  backup left by an interrupted replacement;
- failed/cancelled uploads therefore no longer erase the previously working
  background or icon.


### No built-in visual fallbacks (firmware 1.10.9)

PIXEL PRO 1.10.9 removes the firmware-resident Main Menu background and
screensaver fallback. User-uploaded assets are now the only persistent visual
media.

- If a Main Menu profile has no uploaded background, the device renders a plain
  black background behind its menu icons and dock.
- `MENUBGSTATE|<profile>` reports `STATE=EMPTY|ASSET=NONE|BYTES=0|W=0|H=0`
  when no custom background exists.
- `SAVCLEAR` removes the stored screensaver and leaves the saver state empty.
- `SAVMEDIA` reports `STATE=EMPTY` until a new GIF/JPEG/PXQ is uploaded.
- An empty saver state never auto-starts after the inactivity timeout.
- LittleFS mount failure also leaves media empty; no compiled artwork is
  substituted.


## Main Menu persistence (firmware 1.10.10)

Firmware 1.10.10 keeps the Main Menu action/label configuration in both NVS and
LittleFS. After LittleFS mounts, a valid `/menu_cfg.bin` copy is preferred and
is mirrored back to NVS. This prevents routine application updates from leaving
the Main Menu without its action map if NVS was reset.

When all eight menu slots truly have neither an icon nor an action, the display
shows a small `MAIN MENU EMPTY` sync hint instead of a completely black screen.
This is not a factory background or factory screensaver.

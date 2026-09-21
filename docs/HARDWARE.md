# PIXEL PRO hardware map

Controller: LOLIN/WEMOS ESP32-S2 Mini.

## Phase 1 inputs

| Control | GPIO | Default HID key |
|---|---:|---|
| K1 | 1 | A |
| K2 | 2 | B |
| K3 | 3 | C |
| K4 | 4 | D |
| K5 | 5 | E |
| K6 | 6 | F |
| K7 | 7 | G |
| K8 | 8 | H |

Each switch connects its GPIO directly to GND. Firmware uses INPUT_PULLUP, so no
external pull resistor or diode is required for this direct-key layout.

## Native USB

ESP32-S2 native USB uses GPIO19 (D-) and GPIO20 (D+) through the board USB-C connector.
Do not use GPIO19/GPIO20 for keys.

## PIXEL PRO display

Panel: **ILI9486 3.5-inch, 480×320 landscape**.

Interface: **i8080 / 8080-style 8-bit parallel**.

The shield is the common UNO/Mega2560 8-bit TFT pinout. Its logical LCD bus maps to
the ESP32-S2 Mini as follows:

| LCD signal | UNO/Mega2560 shield pin | ESP32-S2 Mini |
|---|---|---|
| D0 | D8 | D33 |
| D1 | D9 | D34 |
| D2 | D2 | D35 |
| D3 | D3 | D36 |
| D4 | D4 | D37 |
| D5 | D5 | D38 |
| D6 | D6 | D39 |
| D7 | D7 | D40 |
| RD | A0 | D12 |
| WR | A1 | D13 |
| RS / DC | A2 | D14 |
| CS | A3 | D16 |
| RST | A4 | D17 |

The shield's 5V/GND/backlight wiring remains on the shield power pins. The display
logic is driven by the ESP32-S2 through the mappings above.

The firmware uses Arduino_GFX with the ILI9486 driver and an 8-bit parallel bus.
The panel is rotated to 480×320 landscape. ILI9486 frame-rate control is set to the
controller step nearest 60 Hz, while PIXEL PRO caps rendered media at 60 FPS.

PIXEL PRO GIF screensavers are no longer expanded from a reduced raw-frame format.
The Windows app sends the original LZW-compressed GIF file and firmware stores that
file in LittleFS, then decodes it on-device with AnimatedGIF.

Supported GIF canvas sizes are:

- 480×320 landscape, displayed directly.
- 320×480 native panel orientation, rotated to the 480×320 landscape screen without
  resizing or 2× upscaling.

Frame timing is respected but clamped to a maximum playback rate of 60 FPS
(minimum 17 ms per frame). Static images remain full 480×320 RGB565. Actual
full-screen GIF FPS depends on decode complexity and the 8-bit i8080 bus throughput.

Additional reserved PIXEL PRO pins:

- D9..D11: three-way profile navigation switch.
- D18: addressable RGB data.

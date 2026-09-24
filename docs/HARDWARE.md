# PIXEL PRO hardware map

Controller: **LOLIN/WEMOS ESP32-S2 Mini**.

Firmware: **1.9.0**.

This is the final pin plan used by the Arduino firmware. Pin names below use the
board silk-screen convention **Dxx**.

## Native USB

The ESP32-S2 native USB connection uses **D19 (USB D-)** and **D20 (USB D+)**
through the board USB-C connector.

Do not reuse D19 or D20 for keys, encoder, RGB, display, or touch.

## 8-key matrix

PIXEL PRO uses a **2 × 4 diode matrix** instead of eight direct GPIO inputs.

| Matrix signal | ESP32-S2 Mini |
|---|---|
| ROW1 | D1 |
| ROW2 | D2 |
| COL1 | D3 |
| COL2 | D4 |
| COL3 | D5 |
| COL4 | D6 |

Logical key layout:

| | COL1 D3 | COL2 D4 | COL3 D5 | COL4 D6 |
|---|---|---|---|---|
| ROW1 D1 | K1 | K2 | K3 | K4 |
| ROW2 D2 | K5 | K6 | K7 | K8 |

Each switch uses one diode:

`COL -> switch -> diode -> ROW`

The diode stripe / cathode points toward the **ROW** side. Firmware scans one row
low at a time while all columns use internal pull-ups. Unselected rows are left
high-impedance to avoid cross-row current.

## EC11 encoder

| Encoder signal | ESP32-S2 Mini |
|---|---|
| A / CLK | D7 |
| B / DT | D8 |
| SW | D21 |
| Common | GND |

Firmware uses internal pull-ups.

Default standalone behavior:

- clockwise: Volume Up
- counter-clockwise: Volume Down
- press: Mute

The decoder uses a Gray-code transition table and ignores invalid two-bit jumps,
which reduces mechanical bounce without blocking the main loop.

## Reserved profile navigation

D9, D10, and D11 remain reserved for the physical profile-navigation control.
They are not consumed by the matrix, encoder, LCD, touch, RGB, or native USB.

## PIXEL PRO display

Panel: **3.5-inch ILI9486, 480 × 320 landscape**.

Interface: **i8080 / 8080-style 8-bit parallel**.

| Shield signal | Common UNO shield pin | ESP32-S2 Mini |
|---|---|---|
| LCD_D0 | D8 | D33 |
| LCD_D1 | D9 | D34 |
| LCD_D2 | D2 | D35 |
| LCD_D3 | D3 | D36 |
| LCD_D4 | D4 | D37 |
| LCD_D5 | D5 | D38 |
| LCD_D6 | D6 | D39 |
| LCD_D7 | D7 | D40 |
| LCD_RD | A0 | D12 |
| LCD_WR | A1 | D13 |
| LCD_RS / DC | A2 | D14 |
| LCD_CS | A3 | D16 |
| LCD_RST | A4 | D17 |
| 5V | 5V | 5V |
| GND | GND | GND |

The firmware uses Arduino_GFX with `Arduino_ESP32PAR8` and
`Arduino_ILI9486`, rotation 1, giving a logical 480 × 320 landscape canvas.

## 4-wire resistive touch

The shield in PIXEL PRO uses a **4-wire resistive touch panel with no separate
XPT2046 controller**. Four touch electrodes are shared with LCD signals.

| Touch electrode | Shared shield signal | ESP32-S2 Mini |
|---|---|---|
| YP / Y+ | LCD_WR / A1 | D13 |
| XM / X- | LCD_RS / A2 | D14 |
| XP / X+ | LCD_D6 / D6 | D39 |
| YM / Y- | LCD_D7 / D7 | D40 |

No additional touch wires are required beyond the LCD wiring.

D13 and D14 are ADC-capable on ESP32-S2. During a touch sample firmware first
drives **LCD_CS high** to deselect the ILI9486, temporarily changes the four shared
pins into the resistive measurement network, samples the ADC, then restores the
pins to the 8080 LCD bus. This prevents touch reads from becoming accidental LCD
write cycles.

Touch is sampled about every 24 ms. The first touch while the screensaver is
active only wakes the display; it does not trigger an action. On the Main Menu,
a normal touch on one of the eight icon cells emits the corresponding Lumi Action.

Default calibration is suitable as a starting point for the common shield and can
be changed over CDC:

- `GET_TOUCH_CAL`
- `SET_TOUCH_CAL|xMin|xMax|yMin|yMax|flags`
- `RESET_TOUCH_CAL`

Flags:

- bit 0 / 1: swap X/Y
- bit 1 / 2: invert X
- bit 2 / 4: invert Y

The factory default is swap X/Y + invert Y, matching ILI9486 rotation 1.

## RGB

Per-key WS2812/SK6812 data is on **D18**.

Physical LED order:

- LED1 -> K1
- LED2 -> K2
- LED3 -> K3
- LED4 -> K4
- LED5 -> K8
- LED6 -> K7
- LED7 -> K6
- LED8 -> K5

## Onboard status LED

The LOLIN/WEMOS ESP32-S2 Mini onboard blue LED is **D15**.

- USB HID ready: solid ON
- USB not enumerated: blink every 500 ms

## microSD on the TFT shield

The TFT shield also exposes:

- SD_SCK
- SD_DO
- SD_DI
- SD_SS

PIXEL PRO firmware 1.9.0 does **not** use the shield microSD slot. Screensaver and
Main Menu media remain in ESP32 LittleFS. Leave the four SD pins unconnected unless
a later firmware revision explicitly enables external SD storage.

## Complete ESP32-S2 Mini allocation

| Pin | PIXEL PRO function |
|---|---|
| D1 | Matrix ROW1 |
| D2 | Matrix ROW2 |
| D3 | Matrix COL1 |
| D4 | Matrix COL2 |
| D5 | Matrix COL3 |
| D6 | Matrix COL4 |
| D7 | Encoder A |
| D8 | Encoder B |
| D9 | Reserved profile control |
| D10 | Reserved profile control |
| D11 | Reserved profile control |
| D12 | LCD_RD |
| D13 | LCD_WR + Touch YP |
| D14 | LCD_RS/DC + Touch XM |
| D15 | Onboard USB status LED |
| D16 | LCD_CS |
| D17 | LCD_RST |
| D18 | WS2812/SK6812 DATA |
| D19 | Native USB D- |
| D20 | Native USB D+ |
| D21 | Encoder SW |
| D33 | LCD_D0 |
| D34 | LCD_D1 |
| D35 | LCD_D2 |
| D36 | LCD_D3 |
| D37 | LCD_D4 |
| D38 | LCD_D5 |
| D39 | LCD_D6 + Touch XP |
| D40 | LCD_D7 + Touch YM |

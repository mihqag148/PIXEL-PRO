# PIXEL PRO hardware map

Controller: **LOLIN/WEMOS ESP32-S2 Mini**.

Firmware: **1.9.3**.

This pin plan is arranged for easy hand-wiring on the S2 Mini: low-numbered pins
are grouped by function on the left-side headers, while the 8-bit LCD data bus is
kept on D33..D40 on the right-side headers.

All names below use the S2 Mini silk-screen style **Dxx**.

## Native USB

Native USB remains untouched:

| Function | S2 Mini |
|---|---|
| USB D- | D19 |
| USB D+ | D20 |

Do not use D19/D20 for anything else.

## 8-key matrix

| Matrix signal | S2 Mini |
|---|---|
| ROW1 | D1 |
| ROW2 | D2 |
| COL1 | D3 |
| COL2 | D4 |
| COL3 | D5 |
| COL4 | D6 |

Logical layout:

| | COL1 D3 | COL2 D4 | COL3 D5 | COL4 D6 |
|---|---|---|---|---|
| ROW1 D1 | K1 | K2 | K3 | K4 |
| ROW2 D2 | K5 | K6 | K7 | K8 |

Each switch uses one diode:

`COL -> switch -> diode -> ROW`

The diode stripe/cathode points toward ROW.

## Horizontal roller encoder

PIXEL PRO uses the low-profile EVQWGD001-style horizontal roller.

| Roller signal | S2 Mini |
|---|---|
| A | D7 |
| B | D8 |
| Push switch | D21 |
| Encoder common | GND |
| Other switch contact | GND |
| NC pad, if present | leave open |

Default behavior:

- roll clockwise: Volume Up
- roll counter-clockwise: Volume Down
- press: Mute

D7/D8/D21 use internal pull-ups.

## microSD on the TFT shield

Firmware 1.9.3 enables the TFT shield's microSD slot using dedicated hardware SPI.

The four labels printed on the TFT PCB wire directly in order:

| TFT shield SD pin | Function | S2 Mini |
|---|---|---|
| SD_SCK | SPI clock | D9 |
| SD_DO | MISO | D10 |
| SD_DI | MOSI | D11 |
| SD_SS | chip select | D12 |

This D9 -> D12 sequential mapping is intentional so the four SD wires remain
together and easy to identify.

Firmware starts the card at 20 MHz and exposes:

- `SDINFO` or `GET_SD`
- `SDREMOUNT`
- `SDTEST`

`SDTEST` writes, reads, verifies, and deletes a small temporary file. It is useful
for checking the soldered SD wiring.

The card is optional: PIXEL PRO still boots normally if no card is inserted.

Current screensaver/Main Menu storage remains LittleFS. The SD volume is mounted
and available to firmware, but existing LumiPad media commands are not silently
moved to the card in 1.9.3.

## 3.5-inch ILI9486 8-bit display

Panel: **ILI9486 480 x 320**, landscape, i8080 8-bit.

| TFT shield signal | S2 Mini |
|---|---|
| LCD_D0 | D33 |
| LCD_D1 | D34 |
| LCD_D2 | D35 |
| LCD_D3 | D36 |
| LCD_D4 | D37 |
| LCD_D5 | D38 |
| LCD_D6 | D39 |
| LCD_D7 | D40 |
| LCD_WR | D13 |
| LCD_RS / DC | D14 |
| LCD_CS | D16 |
| LCD_RST | D17 |
| LCD_RD | **3V3, not a GPIO** |
| 5V | 5V / VBUS |
| GND | GND |

### Why LCD_RD is tied high

PIXEL PRO only writes display pixels; Arduino_GFX does not need RD for this
write-only parallel path. LCD_RD is therefore tied permanently HIGH to the S2
Mini **3V3** rail.

This frees D12 for SD_SS without losing display functionality.

Do **not** connect LCD_RD to D12 in firmware 1.9.3.

## Resistive touch

The 4-wire resistive touch shares existing LCD wires:

| Touch electrode | Shared LCD signal | S2 Mini |
|---|---|---|
| YP / Y+ | LCD_WR | D13 |
| XM / X- | LCD_RS/DC | D14 |
| XP / X+ | LCD_D6 | D39 |
| YM / Y- | LCD_D7 | D40 |

No extra touch wires are required.

During touch sampling firmware deselects the LCD, samples the resistive network,
then restores the LCD bus. LCD_RD is already held HIGH in hardware.

## RGB

| Function | S2 Mini |
|---|---|
| WS2812/SK6812 data | D18 |
| LED supply | 5V |
| LED ground | GND |

Logical/physical order:

- LED1 -> K1
- LED2 -> K2
- LED3 -> K3
- LED4 -> K4
- LED5 -> K8
- LED6 -> K7
- LED7 -> K6
- LED8 -> K5

## Onboard status LED

D15 remains the S2 Mini onboard blue status LED.

- USB HID ready: solid ON
- USB not enumerated: blink every 500 ms

## Final pin allocation

| S2 Mini | Function |
|---|---|
| D1 | Matrix ROW1 |
| D2 | Matrix ROW2 |
| D3 | Matrix COL1 |
| D4 | Matrix COL2 |
| D5 | Matrix COL3 |
| D6 | Matrix COL4 |
| D7 | Roller A |
| D8 | Roller B |
| D9 | SD_SCK |
| D10 | SD_DO / MISO |
| D11 | SD_DI / MOSI |
| D12 | SD_SS / CS |
| D13 | LCD_WR + Touch YP |
| D14 | LCD_RS/DC + Touch XM |
| D15 | onboard status LED |
| D16 | LCD_CS |
| D17 | LCD_RST |
| D18 | RGB DATA |
| D19 | USB D- |
| D20 | USB D+ |
| D21 | Roller push |
| D33 | LCD_D0 |
| D34 | LCD_D1 |
| D35 | LCD_D2 |
| D36 | LCD_D3 |
| D37 | LCD_D4 |
| D38 | LCD_D5 |
| D39 | LCD_D6 + Touch XP |
| D40 | LCD_D7 + Touch YM |
| 3V3 | LCD_RD permanently HIGH |
| 5V/VBUS | TFT + RGB supply |
| GND | common ground |

## Wiring groups for hand assembly

For the cleanest harness, make four groups:

1. **Keys:** D1-D6
2. **Roller + SD:** D7, D8, D9-D12, with D21 as the separate roller-push wire
3. **LCD control:** D13, D14, D16, D17 plus LCD_RD -> 3V3
4. **LCD data:** D33-D40

This leaves USB D19/D20 untouched and keeps the TFT's four SD wires on one
consecutive D9-D12 block.

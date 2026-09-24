# PIXEL PRO hardware map

Controller: **LOLIN/WEMOS ESP32-S2 Mini**.

Firmware: **1.9.9**.

All controller pins below use the board silk-screen names **Dxx**. Do not convert
this document to raw ESP32 pin naming when wiring.

## Final ESP32-S2 Mini allocation

| S2 Mini pin | PIXEL PRO function |
|---|---|
| D1 | Key matrix ROW1 |
| D2 | Key matrix ROW2 |
| D3 | Key matrix COL1 |
| D4 | Key matrix COL2 |
| D5 | Key matrix COL3 |
| D6 | Key matrix COL4 |
| D7 | Roller A |
| D8 | Roller B |
| D9 | TFT microSD SD_SCK |
| D10 | TFT microSD SD_DO / MISO |
| D11 | TFT microSD SD_DI / MOSI |
| D12 | TFT microSD SD_SS / CS |
| D13 | LCD_WR + touch YP |
| D14 | LCD_RS/DC + touch XM |
| D15 | WS2812/SK6812 DATA |
| D16 | LCD_CS |
| D17 | Expansion MODULE_SCL |
| D18 | Expansion MODULE_SDA |
| D19 | Native USB D- |
| D20 | Native USB D+ |
| D21 | Roller push |
| D33 | LCD_D0 |
| D34 | LCD_D1 |
| D35 | LCD_D2 |
| D36 | LCD_D3 |
| D37 | LCD_D4 |
| D38 | LCD_D5 |
| D39 | LCD_D6 + touch XP |
| D40 | LCD_D7 + touch YM |
| EN | LCD_RST + PCA9546A RESET |
| 3V3 | LCD_RD + PCA9546A VDD + I2C pull-ups |
| 5V/VBUS | TFT + RGB + magnetic module power |
| GND | Common ground |

D19 and D20 belong to native USB and must not be reused.

## 8-key matrix

The keyboard remains physically and electrically **2 rows x 4 columns**.

| | COL1 D3 | COL2 D4 | COL3 D5 | COL4 D6 |
|---|---|---|---|---|
| ROW1 D1 | K1 | K2 | K3 | K4 |
| ROW2 D2 | K5 | K6 | K7 | K8 |

Each key uses one diode:

`COL -> switch -> diode -> ROW`

The stripe/cathode end of the diode points toward the ROW wire.

Per-key wiring:

- K1: D3 -> switch -> diode -> D1
- K2: D4 -> switch -> diode -> D1
- K3: D5 -> switch -> diode -> D1
- K4: D6 -> switch -> diode -> D1
- K5: D3 -> switch -> diode -> D2
- K6: D4 -> switch -> diode -> D2
- K7: D5 -> switch -> diode -> D2
- K8: D6 -> switch -> diode -> D2

## Horizontal roller

PIXEL PRO uses the low-profile EVQWGD001-style horizontal roller.

| Roller contact | S2 Mini |
|---|---|
| A | D7 |
| B | D8 |
| encoder COM | GND |
| push contact 1 | D21 |
| push contact 2 | GND |
| NC pad, if present | leave open |

D7, D8 and D21 use internal pull-ups. A roller press therefore pulls D21 LOW.

Default firmware behavior:

- roll one direction: Volume Up
- roll the other direction: Volume Down
- press: Mute

If the physical roll direction is reversed, swap D7 and D8.

## TFT microSD

The TFT shield microSD remains a dedicated 4-wire SPI group:

| TFT label | S2 Mini |
|---|---|
| SD_SCK | D9 |
| SD_DO | D10 |
| SD_DI | D11 |
| SD_SS | D12 |

Firmware mounts the card at 20 MHz. The card is optional.

CDC test commands:

- `SDINFO` or `GET_SD`
- `SDREMOUNT`
- `SDTEST`

## 3.5-inch 480x320 MCUFRIEND display

Panel: **3.5-inch 480 x 320 MCUFRIEND-style shield**, landscape, i8080 8-bit.

Firmware 1.9.9 mirrors the **MCUFRIEND_kbv controller-ID 0x1581 / R61581**
initialization used by the shop example's `readID() -> begin(ID)` path, then
rotates the 320x480 controller memory to 480x320 landscape. This adds the missing
frame/interface/timing setup and explicitly forces normal (non-inverted) display
polarity. The shop sketch's `//ID=0x9341` text is only a touch calibration
comment; it is not the panel driver selection.

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
| LCD_RST | **EN** |
| LCD_RD | **3V3** |
| 5V | 5V / VBUS |
| GND | GND |

**No display rewiring is required when moving from v1.9.6/v1.9.7 to v1.9.8.**
Keep LCD_RST on EN and LCD_RD on 3V3.

### LCD_RST wiring

D17 is no longer used by LCD_RST.

Connect **LCD_RST directly to EN** on the S2 Mini. EN goes LOW when the main
controller resets, so the TFT receives a reset at the same time. Firmware sets the
Arduino_GFX reset pin to undefined because reset is now hardware-linked.

### LCD_RD

PIXEL PRO uses a write-only parallel display path. Connect **LCD_RD directly to
3V3**. Do not connect LCD_RD to D12.

## Resistive touch

No additional touch pins are required. Touch shares four LCD wires. Firmware
1.9.9 now reproduces the shop sketch's TouchScreen.h electrical sequence,
300-ohm plate-pressure calculation and supplied calibration values:

| Touch electrode | Shared signal | S2 Mini |
|---|---|---|
| YP / Y+ | LCD_WR | D13 |
| XM / X- | LCD_RS/DC | D14 |
| XP / X+ | LCD_D6 | D39 |
| YM / Y- | LCD_D7 | D40 |

Shop calibration reproduced in firmware:

- TS_LEFT = 907
- TS_RT = 136
- TS_TOP = 942
- TS_BOT = 139
- pressure window = 200..1000
- X-plate resistance = 300 ohm

Firmware deselects the LCD before every touch measurement, samples the resistive
panel, then restores the 8-bit LCD bus.

## RGB

RGB data stays on **D15** in firmware 1.9.8.

| RGB connection | S2 Mini |
|---|---|
| DATA IN LED1 | D15 |
| +5V | 5V |
| GND | GND |

Recommended: place a 220-470 ohm series resistor between D15 and LED1 DATA IN.

The S2 Mini onboard blue LED is physically connected to D15 through its own
resistor. It is no longer used as a separate USB-status LED. D15 is driven as the
push-pull addressable-RGB signal instead.

Physical LED order remains:

- LED1 -> K1
- LED2 -> K2
- LED3 -> K3
- LED4 -> K4
- LED5 -> K8
- LED6 -> K7
- LED7 -> K6
- LED8 -> K5

## Magnetic expansion modules

Firmware 1.9.8 keeps exactly two S2 Mini pins for the expansion bus:

| Expansion bus | S2 Mini |
|---|---|
| MODULE_SDA | **D18** |
| MODULE_SCL | **D17** |

These two pins connect to the **upstream SDA/SCL** of one PCA9546A.

### PCA9546A main-side wiring

Use PCA9546A at 3.3 V.

| PCA9546A signal | Connect to |
|---|---|
| VDD | 3V3 |
| VSS/GND | GND |
| SDA | D18 |
| SCL | D17 |
| RESET | EN |
| A0 | GND |
| A1 | GND |
| A2 | GND |

A0/A1/A2 LOW gives the mux address used by firmware: **0x70**.

Add:

- 100 nF from PCA9546A VDD to GND, close to the IC
- 4.7 kohm from D18/SDA to 3V3
- 4.7 kohm from D17/SCL to 3V3

### Three magnetic ports

Firmware uses channels 0, 1 and 2 as physical ports 1, 2 and 3.

| PCA9546A channel | Magnetic connector |
|---|---|
| SD0 + SC0 | PORT 1 SDA + SCL |
| SD1 + SC1 | PORT 2 SDA + SCL |
| SD2 + SC2 | PORT 3 SDA + SCL |
| SD3 + SC3 | spare |

Each magnetic connector has four electrical contacts:

`5V | GND | SDA | SCL`

The connector must be mechanically keyed so reversed attachment cannot exchange
5V and GND.

Recommended downstream pull-ups on the PIXEL PRO side:

- each SD0/SD1/SD2 line: 10 kohm to 3V3
- each SC0/SC1/SC2 line: 10 kohm to 3V3

A module ESP receives 5V from the connector and should generate its own 3.3 V
locally. Its SDA/SCL are 3.3 V logic.

All three modules may use the same module address **0x42** because the PCA9546A
isolates the three physical ports. The selected mux channel is therefore also the
physical port ID.

## Module protocol summary

PIXEL PRO scans ports 1-3 continuously. One port is sampled every 2 ms, so each
physical port is revisited roughly every 6 ms.

A module at address 0x42 returns a fixed 16-byte frame:

| Byte | Meaning |
|---|---|
| 0 | 0xA5 magic |
| 1 | protocol version 1 |
| 2 | module type |
| 3 | sequence number |
| 4-5 | 16-bit button mask, little-endian |
| 6 | encoder 1 signed delta |
| 7 | encoder 2 signed delta |
| 8-9 | slider 1, little-endian |
| 10-11 | slider 2, little-endian |
| 12-13 | module ID, little-endian |
| 14 | flags |
| 15 | CRC-8 polynomial 0x07 over bytes 0-14 |

A module should increment the sequence number whenever it publishes new state or
a new relative encoder event.

The main firmware emits connection, disconnection and data events over USB CDC.
See `docs/MODULE_BUS.md`.

## Power

The magnetic connectors carry 5 V, not 3.3 V. Each ESP-based module should have
its own 3.3 V regulator and local decoupling.

Three ESP modules + TFT + eight RGB LEDs can exceed what a weak USB port can
comfortably supply. For a finished product use a properly rated 5 V supply path or
powered USB hub. Do not hard-wire an external 5 V rail back into a PC USB VBUS
without power-path isolation.

## Assembly groups

For clean hand wiring:

1. Keys: D1-D6
2. Roller: D7, D8, D21
3. microSD: D9-D12
4. TFT control/touch: D13, D14, D16; LCD_RST -> EN; LCD_RD -> 3V3
5. RGB: D15
6. Module bus: D17 + D18 -> PCA9546A
7. USB: leave D19/D20 dedicated
8. TFT 8-bit data: D33-D40

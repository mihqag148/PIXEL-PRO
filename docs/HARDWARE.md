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

## eezbotfun-style expansion reserved for later phases

The public eezbotfun design has a 480x320 3.5-inch display, eight hot-swap keys,
profile navigation and RGB. PIXEL PRO reserves:

- GPIO9..11: three-way profile navigation switch.
- GPIO18: addressable RGB data.
- GPIO33..40: 8-bit TFT data bus.
- GPIO12,13,14,16,17: TFT control lines.

These expansion pins are documented now but are intentionally not driven by the phase-1
firmware. Exact TFT controller/control polarity must match the physical display before
the display phase is enabled.

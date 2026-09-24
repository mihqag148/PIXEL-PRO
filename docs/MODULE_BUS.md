# PIXEL PRO magnetic module bus

Firmware: **1.9.6**

Main controller: WEMOS/LOLIN ESP32-S2 Mini.

## Physical topology

PIXEL PRO uses D18 as SDA and D17 as SCL into a PCA9546A 4-channel I2C switch.

- PCA9546A address: 0x70
- module address on every downstream channel: 0x42
- bus speed: 400 kHz
- active module ports: channels 0, 1 and 2
- channel 3: spare

Because every connector is isolated behind its own PCA9546A channel, identical
modules can all keep address 0x42. Firmware identifies the physical connector by
channel:

- channel 0 -> PORT 1
- channel 1 -> PORT 2
- channel 2 -> PORT 3

## Magnetic connector

Each module connector carries:

`5V | GND | SDA | SCL`

The module must regulate 5 V down to the voltage required by its ESP board.

## Module-side I2C contract

Each module ESP is an I2C slave at 7-bit address **0x42**.

When PIXEL PRO performs a 16-byte I2C read, return exactly:

| Byte | Field |
|---|---|
| 0 | magic = 0xA5 |
| 1 | protocol version = 1 |
| 2 | module type |
| 3 | sequence |
| 4 | buttons bits 0-7 |
| 5 | buttons bits 8-15 |
| 6 | encoder 1 delta, signed int8 |
| 7 | encoder 2 delta, signed int8 |
| 8 | slider 1 low byte |
| 9 | slider 1 high byte |
| 10 | slider 2 low byte |
| 11 | slider 2 high byte |
| 12 | module ID low byte |
| 13 | module ID high byte |
| 14 | flags |
| 15 | CRC-8 |

CRC-8 uses polynomial **0x07**, initial value 0x00, no reflection, over bytes
0 through 14.

The sequence field should change whenever a new logical event/state is published.
This prevents a relative encoder delta from being applied repeatedly when the
master polls the same frame more than once.

Suggested type IDs:

- 1 = keypad
- 2 = encoder/roller
- 3 = slider
- 4 = combo controls
- 5-255 = future/vendor-defined

## USB CDC events

Connection:

`MODULE|PORT=1|CONNECTED`

Disconnection:

`MODULE|PORT=1|DISCONNECTED`

Valid new state:

`MODULE_DATA|PORT=1|TYPE=4|ID=123|SEQ=7|BTN=0005|E1=1|E2=0|S1=2048|S2=0|FLAGS=00`

## USB CDC commands

Read current state of all ports:

`GET_MODULES`

Force a full rescan:

`MODULE_SCAN`

Send an arbitrary payload to one module:

`MODULE_TX|<port 1-3>|<hex bytes>`

Example:

`MODULE_TX|2|A50110`

Maximum host-to-module payload is 24 bytes.

## Hot-plug behavior

PIXEL PRO polls one physical port every 2 ms. A port is considered disconnected
after three consecutive address misses. PCA9546A RESET is wired to S2 Mini EN so
all channels return to deselected state whenever the main controller resets.

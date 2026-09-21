# PIXEL PRO USB CDC protocol v1

Transport: native USB CDC ACM. The nominal baud value is 115200; USB CDC does not depend on a physical UART baud clock.

Firmware 1.0.1 exposes only HID keyboard + CDC while the Windows CDC path is validated.

## Host commands

Commands are ASCII, one command per line, terminated by LF.

- HELLO
- GET_INFO
- GET_KEYS
- PING
- REBOOT

## Device responses

HELLO / GET_INFO:

PIXELPRO|1|FW=1.0.1|MCU=ESP32S2|KEYS=8|CAPS=HID,CDC|VID=303A|PID=80C2

GET_KEYS:

KEYS|00

Key transitions are asynchronous:

KEY|1|DOWN
KEY|1|UP
...
KEY|8|DOWN
KEY|8|UP

PING returns:

PONG|PIXELPRO

Unknown commands return ERR|UNKNOWN_COMMAND.

LumiPad identifies PIXEL PRO by probing CDC ports for the PIXELPRO|1| response.

## USB initialization rule

The build intentionally uses a generic ESP32-S2 target with USB CDC On Boot disabled. The firmware registers CDC and HID first and calls USB.begin() exactly once afterwards. This avoids the early USB auto-start behavior of the LOLIN S2 Mini board definition.

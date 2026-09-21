# PIXEL PRO USB CDC protocol v1

Transport: native USB CDC ACM at 115200 baud. The baud value is informational for USB CDC.

The same USB device also exposes standard HID keyboard and Firmware MSC interfaces.

## Host commands

Commands are ASCII, one command per line, terminated by LF.

- HELLO
- GET_INFO
- GET_KEYS
- PING
- REBOOT

## Device responses

HELLO / GET_INFO:

PIXELPRO|1|FW=1.0.0|MCU=ESP32S2|KEYS=8|CAPS=HID,CDC,MSC|VID=303A|PID=80C2

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

LumiPad identifies PIXEL PRO by probing CDC ports for the PIXELPRO|1| response. This
avoids coupling the app to a fragile HID vendor interface.

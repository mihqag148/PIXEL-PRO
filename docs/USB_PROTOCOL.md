# PIXEL PRO USB CDC protocol v1

Transport: native USB CDC ACM. The nominal baud value is 115200; USB CDC does not depend on a physical UART baud clock.

Firmware 1.1.0 exposes HID keyboard + consumer control + CDC and adds persistent keymap storage.

## Host commands

Commands are ASCII, one command per line, terminated by LF.

- HELLO
- GET_INFO
- GET_KEYS
- GET_KEYMAP
- SET_KEYMAP|<8 comma-separated bindings>
- RESET_KEYMAP
- PING
- REBOOT

## HELLO

HELLO / GET_INFO returns:

PIXELPRO|1|FW=1.1.0|MCU=ESP32S2|KEYS=8|CAPS=HID,CDC,KEYMAP|VID=303A|PID=80C2

## Keymap format

GET_KEYMAP returns exactly eight bindings:

KEYMAP|K:4:0,K:5:0,K:6:0,K:7:0,K:8:0,K:9:0,K:10:0,K:11:0

Binding types:

- K:<keyboard usage>:<modifier mask>
- C:<consumer usage>:0
- D:0:0

Keyboard usages are USB HID keyboard-page usage IDs. Modifier mask bits are:

- bit 0 / 1 = Left Ctrl
- bit 1 / 2 = Left Shift
- bit 2 / 4 = Left Alt
- bit 3 / 8 = Left GUI / Windows

Example Ctrl+C:

K:6:1

Example Play/Pause consumer control:

C:205:0

SET_KEYMAP stores the complete eight-key map in NVS and returns:

OK|KEYMAP

RESET_KEYMAP restores K1=A through K8=H, stores it in NVS, and returns:

OK|KEYMAP_RESET

## Key state

GET_KEYS returns:

KEYS|00

Physical key transitions are asynchronous:

KEY|1|DOWN
KEY|1|UP
...
KEY|8|DOWN
KEY|8|UP

PING returns:

PONG|PIXELPRO

Unknown commands return ERR|UNKNOWN_COMMAND.

The build intentionally uses a generic ESP32-S2 target with USB CDC On Boot disabled. CDC and HID descriptors are registered before the single USB.begin().

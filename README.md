# PIXEL PRO ZMK

USB-only ZMK keyboard for LOLIN/WEMOS ESP32-S2 Mini, with a separate
native Zephyr HID interface for LumiPad Windows.

- [New wiring](docs/HARDWARE.md): K1–K8 on GPIO1–8, contacts to GND.
- [Flash and hardware acceptance](docs/FLASH.md)
- [Companion protocol](docs/USB_PROTOCOL.md)
- Desktop app: https://github.com/mihqag148/Lumipad-APP

Phase 1: A–H keyboard, optional navigation contacts, connection diagnostics.
LCD/RGB/media features are reserved for phase 2. No encoder.

Build with the pinned config/west.yml manifest and build-zmk.yml workflow.
The native ESP32-S2 USB adaptation is in drivers/usb_dc_pixel_s2.c; it derives
from the Apache-2.0 Zephyr DesignWare driver at commit
10ba6d0cb38bc3d258775d27982f707599320085. Hardware validation is pending.

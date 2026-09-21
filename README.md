# PIXEL PRO

PIXEL PRO is an ESP32-S2 macro pad firmware and Windows companion application target.

This repository now uses the ESP32-S2 native USB peripheral through Arduino-ESP32/TinyUSB.
The active firmware does not depend on keyboard firmware frameworks or browser keymap protocols.

## Phase 1 USB device

The single USB-C connection exposes:

- Standard HID keyboard: works without LumiPad.
- USB CDC serial: LumiPad configuration/diagnostics channel.
- Firmware MSC: recovery/update drive provided by Arduino-ESP32.

The public eezbotfun/8-key-macropad project is used as an architectural reference for its
HID + CDC + storage workflow. This implementation is independent and keeps the PIXEL PRO
hardware/product identity.

## Test keymap

K1..K8 are GPIO1..GPIO8, active-low with internal pull-ups.

K1=A, K2=B, K3=C, K4=D, K5=E, K6=F, K7=G, K8=H.

See docs/HARDWARE.md, docs/USB_PROTOCOL.md and docs/FLASH.md.

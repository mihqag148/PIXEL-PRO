# PIXEL PRO

PIXEL PRO is an ESP32-S2 macro pad firmware and Windows companion application target.

The active firmware uses the ESP32-S2 native USB peripheral through Arduino-ESP32/TinyUSB.

## Phase 1 USB device

The single USB-C connection exposes:

- Standard HID keyboard: works without LumiPad.
- USB CDC serial: LumiPad configuration/diagnostics channel.

Firmware MSC is intentionally disabled in 1.0.1 while HID + CDC are hardware-validated. Storage/update mode will return only after CDC is confirmed stable on Windows.

The public eezbotfun/8-key-macropad project is used as an architectural reference for its HID + CDC + storage workflow. PIXEL PRO uses an independent implementation.

## Test keymap

K1..K8 are GPIO1..GPIO8, active-low with internal pull-ups.

K1=A, K2=B, K3=C, K4=D, K5=E, K6=F, K7=G, K8=H.

See docs/HARDWARE.md, docs/USB_PROTOCOL.md and docs/FLASH.md.

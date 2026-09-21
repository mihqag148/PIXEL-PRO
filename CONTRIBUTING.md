# Contributing to PIXEL PRO

PIXEL PRO uses the ESP32-S2 native USB peripheral through Arduino-ESP32/TinyUSB.

## Pull requests

1. Keep hardware pin changes documented in `docs/HARDWARE.md`.
2. Keep the USB CDC wire format documented in `docs/USB_PROTOCOL.md`.
3. Every pull request must pass the native USB firmware GitHub Actions build.
4. Do not merge generated build directories or local Arduino package caches.
5. Changes to the Windows companion protocol should be coordinated with the LumiPad app repository.

A green compile validates the toolchain and binary packaging. Physical keyboard, CDC and display behavior still require hardware testing on the target board.

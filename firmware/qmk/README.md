# PIXEL PRO real QMK proof build

This directory is a **real QMK core** proof build for the existing ESP32-S2
hardware. It does not call the old ESP-IDF VIA emulator "QMK".

QMK upstream does not support ESP32-S2, so this build pins the experimental
ESP32-S2 QMK fork:

- QMK core: `morganvenable/lalboard-qmk-clone@42e50c9...`
- TinyUSB: `espressif/tinyusb@334e95f...`

The build keeps QMK's real `quantum/via.c`, dynamic keymap, matrix, encoder,
key processing and Raw HID. A small platform patch completes Raw HID support in
that fork's TinyUSB backend. A Lumi dispatcher is inserted **before** VIA only
for packets beginning with `LQ`; normal VIA packets remain handled by QMK.

## First milestone

- 8 matrix keys: GPIO13/14 rows, GPIO33..36 columns
- EC11: GPIO37/38
- EC11 push: GPIO39
- VIA: real QMK VIA
- 5 dynamic layers
- VID/PID: 303A:4009
- LumiPad: HELLO + MEM only for connection bring-up

LCD, GIF/video, RGB and OTA are intentionally not in the first QMK proof build.
They will be reintroduced only after the QMK/VIA USB path is confirmed on real
hardware.

The workflow produces `PIXEL_PRO_QMK_merged.bin`, intended to be flashed at
address 0x0 in ESP32-S2 ROM BOOT mode.

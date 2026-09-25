# Flash PIXEL PRO native USB firmware

## Normal firmware update (preserve settings)

Use `PIXEL_PRO_app.bin` for routine updates when the current PIXEL PRO
partition table is already installed:

```text
python -m esptool --chip esp32s2 --port COMx write_flash 0x10000 PIXEL_PRO_app.bin
```

Writing only the application partition preserves the NVS settings at
`0x9000` and the LittleFS media partition at `0x190000`.

## First install / recovery image

Use `PIXEL_PRO_merged.bin` at `0x0` only for a first install, partition-table
recovery, or a deliberate factory-style reflash. Because a merged image spans
the bootloader, partition table and application ranges, it also writes through
the NVS address range and can reset settings stored there.

1. Hold BOOT, tap RESET, release BOOT to enter ESP32-S2 ROM download mode.
2. Find the ROM COM port. The ROM device commonly appears with Espressif VID 303A and a ROM PID such as 0002.
3. Run:

   python -m esptool --chip esp32s2 --port COMx write_flash 0x0 PIXEL_PRO_merged.bin

4. Release BOOT and tap RESET once. Do not hold BOOT during this reset.

Expected Windows behavior after the application boots:

- PIXEL PRO enumerates as VID 303A / PID 80C2.
- Keyboard HID appears and GPIO1..GPIO8 type A..H.
- A usable USB CDC COM port appears for LumiPad.
- No MSC drive is expected in firmware 1.0.1.

For the fastest HID test, short GPIO1 to GND briefly while Notepad is focused. The host should type the letter A.

For CDC, send HELLO followed by LF. The response must start with PIXELPRO|1|.


## Firmware 1.5.2 media partition

The merged image installs a custom 4 MB partition table with a 1.5 MiB application area and a 2.44 MiB LittleFS media area. Reflashing 1.5.2 changes the partition map, so existing uploaded screensaver media should be considered disposable and may be reformatted on first boot.

Persistent GIF/PXQ/JPEG files live in flash LittleFS. PSRAM is runtime buffering only.


## Main-menu media (firmware 1.6.0)

Persistent PIXEL PRO main-menu artwork shares the LittleFS media partition with
the screensaver. The app limits the background JPEG to 96 KiB and stores up to
48 icons (4 menus × 12 slots) as 40×40 RGB565 files (3,200 bytes each).

Screensaver replacement is destructive by design: the previous screensaver
GIF/PXQ/JPEG is removed before free-space validation for the new screensaver.
Main-menu artwork is independent and is not deleted when the screensaver is
replaced.

PSRAM remains runtime workspace only; all persistent screensaver and main-menu
artwork is stored in Flash/LittleFS.

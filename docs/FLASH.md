# Flash PIXEL PRO ZMK 0.3.0

Target: LOLIN/WEMOS ESP32-S2 Mini, 4 MB flash, new wiring in HARDWARE.md.
The Zephyr ESP_SIMPLE_BOOT image includes boot initialization and is
written at 0x1000 on ESP32-S2. Do not flash it at 0 or use an OTA updater.

1. Install the official esptool package: pip install esptool
2. Hold BOOT (GPIO0), tap RESET, then release BOOT.
3. Identify the newly appeared ROM USB COM port.
4. Run: python -m esptool --chip esp32s2 --port COMx write_flash 0x1000 PIXEL_PRO_ZMK.bin
5. Tap RESET. The device should enumerate as PIXEL PRO ZMK.

The application HID interface is not a flashing port. Repeat BOOT/RESET
to recover even if the application USB driver does not enumerate.
Do not burn eFuses, enable secure boot, or alter ROM download settings.

Acceptance on hardware: type A–H without LumiPad, hold multiple keys,
then open LumiPad Diagnostics and check matching DOWN/UP events,
navigation (if wired), unplug/replug and reboot. Compilation alone is
not evidence that these hardware checks passed.

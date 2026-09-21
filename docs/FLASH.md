# Flash PIXEL PRO native USB firmware

Artifact: PIXEL_PRO_merged.bin

1. Hold BOOT, tap RESET, release BOOT to enter ESP32-S2 ROM download mode.
2. Find the ROM COM port.
3. Run:

   python -m esptool --chip esp32s2 --port COMx write_flash 0x0 PIXEL_PRO_merged.bin

4. Tap RESET once.

Expected Windows behavior after boot:

- Keyboard HID appears and GPIO1..GPIO8 type A..H.
- A USB CDC COM port appears for LumiPad.
- A firmware-update MSC drive may appear.

For the fastest hardware test, short GPIO1 to GND briefly while Notepad is focused.
The host should type the letter A.

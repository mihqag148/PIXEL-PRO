MCU = esp32s2

BOOTMAGIC_ENABLE = no
MOUSEKEY_ENABLE = no
EXTRAKEY_ENABLE = yes
CONSOLE_ENABLE = no
COMMAND_ENABLE = no
NKRO_ENABLE = no
AUDIO_ENABLE = no
RGBLIGHT_ENABLE = no
BACKLIGHT_ENABLE = no
MIDI_ENABLE = no
UNICODE_ENABLE = no
BLUETOOTH_ENABLE = no

# Keep bring-up independent from the ESP32 FAT wear-level EEPROM backend.
# VIA still works with QMK's transient RAM EEPROM; persistence can be restored
# after USB/Raw HID is proven stable on physical hardware.
EEPROM_DRIVER = transient

# Do not enter the ESP32 light-sleep path while USB is being brought up.
NO_USB_STARTUP_CHECK = yes
NO_SUSPEND_POWER_DOWN = yes

CUSTOM_MATRIX = yes
SRC += matrix.c
ENCODER_ENABLE = yes
LTO_ENABLE = no

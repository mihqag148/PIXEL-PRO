#pragma once

#define VENDOR_ID       0x303A
#define PRODUCT_ID      0x4009
#define DEVICE_VER      0x0201
#define MANUFACTURER    Lumi3D
#define PRODUCT         PIXEL_PRO_QMK
#define SERIAL_NUMBER   PIXELPRO_QMK_0201

/*
 * Two physical matrix rows (K1..K8) plus one virtual row for the EC11
 * push switch on GPIO39. The custom matrix scanner implements this.
 */
#define MATRIX_ROWS 3
#define MATRIX_COLS 4
#define DEBOUNCE 5

#define DYNAMIC_KEYMAP_LAYER_COUNT 5
#define DYNAMIC_KEYMAP_EEPROM_MAX_ADDR 2047
#define EEPROM_SIZE 2048

#define RAW_USAGE_PAGE 0xFF60
#define RAW_USAGE_ID   0x61

#define ENCODERS_PAD_A { 37 }
#define ENCODERS_PAD_B { 38 }
#define ENCODER_RESOLUTION 4

#define PIXEL_LUMI_ENABLE

#define NO_ACTION_MACRO
#define NO_ACTION_FUNCTION

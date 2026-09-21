/*
 * This component exposes the ESP-IDF include set to the external QMK build
 * and attaches the resulting QMK static archive to the final link.
 * app_main() is provided by QMK's TinyUSB platform backend.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/periph_ctrl.h"
#include "driver/rmt.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_rom_gpio.h"
#include "hal/gpio_ll.h"
#include "hal/usb_hal.h"
#include "soc/usb_periph.h"

void pixel_pro_qmk_idf_include_probe(void) {
}

/*
 * PIXEL PRO v0.1.7
 * ESP32-S2 Mini / ESP-IDF + TinyUSB
 *
 * USB interface 0: standard HID keyboard
 * USB interface 1: QMK/VIA-compatible Raw HID
 *   Usage Page 0xFF60
 *   Usage      0x61
 *   Report ID  0
 *   32-byte IN/OUT reports
 *
 * BOOT button temporarily acts as K1 so the bare board can be tested.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/gpio.h"

#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"
#include "mbedtls/base64.h"

#define FW_VERSION "0.1.7"

#define USB_VID 0x303A
#define USB_PID 0x4009

#define BOOT_GPIO GPIO_NUM_0
#define LED_GPIO  GPIO_NUM_15

#define RAW_SIZE 32
#define LUMI_MAGIC0 'L'
#define LUMI_MAGIC1 'Q'
#define LUMI_FRAME_VERSION 1

#define VIA_LAYERS 5
#define MATRIX_ROWS 2
#define MATRIX_COLS 4
#define KEY_COUNT (MATRIX_ROWS * MATRIX_COLS)

enum {
    ITF_NUM_KEYBOARD = 0,
    ITF_NUM_RAW,
    ITF_NUM_TOTAL,
};

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_KEYBOARD,
    STRID_RAW,
};

#define EPNUM_KEYBOARD_IN 0x81
#define EPNUM_RAW_OUT     0x02
/*
 * Keep Raw HID IN on a different endpoint number from Raw HID OUT.
 * This is valid on ESP32-S2 and avoids host/controller edge cases seen with
 * paired EP2 IN/OUT during VIA/HidSharp writes.
 */
#define EPNUM_RAW_IN      0x83

#define TUSB_DESC_TOTAL_LEN \
    (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

static const char *TAG = "pixel_pro";

static const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0107,
    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,
    .bNumConfigurations = 0x01,
};

static const char *string_descriptor[] = {
    (char[]){0x09, 0x04},
    "Lumi3D",
    "PIXEL PRO",
    "PIXELPRO-0107",
    "PIXEL PRO Keyboard",
    "PIXEL PRO VIA Raw HID",
};

static const uint8_t keyboard_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

/*
 * Match QMK raw_hid / VIA discovery exactly:
 * top-level Usage Page 0xFF60, Usage 0x61, 32-byte input/output, no report ID.
 */
static const uint8_t raw_report_descriptor[] = {
    0x06, 0x60, 0xFF,       // Usage Page (Vendor 0xFF60)
    0x09, 0x61,             // Usage (0x61)
    0xA1, 0x01,             // Collection (Application)

    0x09, 0x62,             //   Usage (0x62)
    0x15, 0x00,             //   Logical Minimum (0)
    0x26, 0xFF, 0x00,       //   Logical Maximum (255)
    0x75, 0x08,             //   Report Size (8)
    0x95, 0x20,             //   Report Count (32)
    0x81, 0x02,             //   Input (Data,Var,Abs)

    0x09, 0x63,             //   Usage (0x63)
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x75, 0x08,
    0x95, 0x20,
    0x91, 0x02,             //   Output (Data,Var,Abs)

    0xC0,
};

static const uint8_t configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(
        1,
        ITF_NUM_TOTAL,
        0,
        TUSB_DESC_TOTAL_LEN,
        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP,
        100
    ),

    TUD_HID_DESCRIPTOR(
        ITF_NUM_KEYBOARD,
        STRID_KEYBOARD,
        HID_ITF_PROTOCOL_KEYBOARD,
        sizeof(keyboard_report_descriptor),
        EPNUM_KEYBOARD_IN,
        8,
        1
    ),

    TUD_HID_INOUT_DESCRIPTOR(
        ITF_NUM_RAW,
        STRID_RAW,
        HID_ITF_PROTOCOL_NONE,
        sizeof(raw_report_descriptor),
        EPNUM_RAW_OUT,
        EPNUM_RAW_IN,
        RAW_SIZE,
        1
    ),
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    if (instance == ITF_NUM_KEYBOARD) {
        return keyboard_report_descriptor;
    }
    if (instance == ITF_NUM_RAW) {
        return raw_report_descriptor;
    }
    return NULL;
}

uint16_t tud_hid_get_report_cb(
    uint8_t instance,
    uint8_t report_id,
    hid_report_type_t report_type,
    uint8_t *buffer,
    uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

typedef struct {
    uint8_t data[RAW_SIZE];
} raw_packet_t;

static QueueHandle_t raw_queue;
static QueueHandle_t raw_tx_queue;

static void build_via_response(
    const uint8_t req[RAW_SIZE],
    uint8_t resp[RAW_SIZE]);

void tud_hid_set_report_cb(
    uint8_t instance,
    uint8_t report_id,
    hid_report_type_t report_type,
    uint8_t const *buffer,
    uint16_t bufsize)
{
    (void)report_id;
    (void)report_type;

    if (instance != ITF_NUM_RAW ||
        buffer == NULL ||
        bufsize < RAW_SIZE ||
        raw_queue == NULL ||
        raw_tx_queue == NULL) {
        return;
    }

    /*
     * VIA/WebHID expects a very fast request/response turnaround.
     * Reply to VIA packets directly from TinyUSB's OUT callback, matching
     * TinyUSB's official generic HID IN/OUT example. Lumi's longer protocol
     * remains queued so OTA/NVS work never blocks the USB callback.
     */
    if (buffer[0] != LUMI_MAGIC0 ||
        buffer[1] != LUMI_MAGIC1 ||
        buffer[2] != LUMI_FRAME_VERSION) {
        raw_packet_t response;
        build_via_response(buffer, response.data);

        if (!tud_hid_n_report(
                ITF_NUM_RAW,
                0,
                response.data,
                RAW_SIZE)) {
            xQueueSend(raw_tx_queue, &response, 0);
        }
        return;
    }

    raw_packet_t packet;
    memcpy(packet.data, buffer, RAW_SIZE);
    xQueueSend(raw_queue, &packet, 0);
}

static bool raw_send(const uint8_t data[RAW_SIZE])
{
    if (!tud_mounted() || !tud_hid_n_ready(ITF_NUM_RAW)) {
        return false;
    }
    return tud_hid_n_report(ITF_NUM_RAW, 0, data, RAW_SIZE);
}

/* ---------------- VIA keymap ---------------- */

static uint16_t keymap[VIA_LAYERS][MATRIX_ROWS][MATRIX_COLS];
static nvs_handle_t via_nvs;
static bool via_nvs_opened;
static volatile bool via_save_pending;

static void keymap_defaults(void)
{
    memset(keymap, 0, sizeof(keymap));

    // Layer 0: A..H
    for (int i = 0; i < KEY_COUNT; ++i) {
        keymap[0][i / MATRIX_COLS][i % MATRIX_COLS] = (uint16_t)(0x0004 + i);
    }

    // Layers 1..4: F13..F20
    for (int layer = 1; layer < VIA_LAYERS; ++layer) {
        for (int i = 0; i < KEY_COUNT; ++i) {
            keymap[layer][i / MATRIX_COLS][i % MATRIX_COLS] = (uint16_t)(0x0068 + i);
        }
    }
}

static void keymap_save(void)
{
    if (!via_nvs_opened) return;
    nvs_set_blob(via_nvs, "keymap", keymap, sizeof(keymap));
    nvs_commit(via_nvs);
}

static void keymap_load(void)
{
    keymap_defaults();

    if (nvs_open("via", NVS_READWRITE, &via_nvs) != ESP_OK) {
        via_nvs_opened = false;
        return;
    }

    via_nvs_opened = true;

    size_t size = sizeof(keymap);
    if (nvs_get_blob(via_nvs, "keymap", keymap, &size) != ESP_OK || size != sizeof(keymap)) {
        keymap_defaults();
        keymap_save();
    }
}

static uint8_t keymap_byte_at(uint16_t offset)
{
    const uint16_t byte_count = sizeof(keymap);
    if (offset >= byte_count) return 0;

    const uint8_t *bytes = (const uint8_t *)keymap;

    /*
     * QMK dynamic keymap buffer is big-endian per keycode.
     * Native ESP32 is little-endian, so swap byte order within each uint16_t.
     */
    uint16_t word_base = offset & (uint16_t)~1U;
    if ((offset & 1U) == 0) {
        return bytes[word_base + 1];
    }
    return bytes[word_base];
}

static void set_keymap_byte_at(uint16_t offset, uint8_t value)
{
    const uint16_t byte_count = sizeof(keymap);
    if (offset >= byte_count) return;

    uint8_t *bytes = (uint8_t *)keymap;
    uint16_t word_base = offset & (uint16_t)~1U;

    if ((offset & 1U) == 0) {
        bytes[word_base + 1] = value;
    } else {
        bytes[word_base] = value;
    }
}

/* Current QMK VIA protocol version */
#define VIA_PROTOCOL_VERSION 0x000D

static void build_via_response(
    const uint8_t req[RAW_SIZE],
    uint8_t resp[RAW_SIZE])
{
    memcpy(resp, req, RAW_SIZE);

    switch (req[0]) {
        case 0x01: // id_get_protocol_version
            resp[1] = (VIA_PROTOCOL_VERSION >> 8) & 0xFF;
            resp[2] = VIA_PROTOCOL_VERSION & 0xFF;
            break;

        case 0x02: { // id_get_keyboard_value
            switch (req[1]) {
                case 0x01: { // uptime
                    uint32_t value = (uint32_t)(esp_timer_get_time() / 1000ULL);
                    resp[2] = (value >> 24) & 0xFF;
                    resp[3] = (value >> 16) & 0xFF;
                    resp[4] = (value >> 8) & 0xFF;
                    resp[5] = value & 0xFF;
                    break;
                }

                case 0x02: // layout options = 0
                    resp[2] = 0;
                    resp[3] = 0;
                    resp[4] = 0;
                    resp[5] = 0;
                    break;

                case 0x03: // switch matrix state, no physical matrix yet
                    memset(&resp[3], 0, RAW_SIZE - 3);
                    break;

                case 0x04: { // firmware version
                    uint32_t value = 0x00000107;
                    resp[2] = (value >> 24) & 0xFF;
                    resp[3] = (value >> 16) & 0xFF;
                    resp[4] = (value >> 8) & 0xFF;
                    resp[5] = value & 0xFF;
                    break;
                }

                case 0x06: // keycodes version; 0 = generic compatibility
                    resp[2] = 0;
                    resp[3] = 0;
                    resp[4] = 0;
                    resp[5] = 0;
                    break;

                default:
                    resp[0] = 0xFF;
                    break;
            }
            break;
        }

        case 0x03: // id_set_keyboard_value
            // Layout options/device indication accepted, no-op for bring-up.
            if (req[1] != 0x02 && req[1] != 0x05) {
                resp[0] = 0xFF;
            }
            break;

        case 0x04: { // dynamic_keymap_get_keycode
            uint8_t layer = req[1];
            uint8_t row = req[2];
            uint8_t col = req[3];

            uint16_t kc = 0;
            if (layer < VIA_LAYERS && row < MATRIX_ROWS && col < MATRIX_COLS) {
                kc = keymap[layer][row][col];
            }

            resp[4] = (kc >> 8) & 0xFF;
            resp[5] = kc & 0xFF;
            break;
        }

        case 0x05: { // dynamic_keymap_set_keycode
            uint8_t layer = req[1];
            uint8_t row = req[2];
            uint8_t col = req[3];

            if (layer < VIA_LAYERS && row < MATRIX_ROWS && col < MATRIX_COLS) {
                keymap[layer][row][col] = ((uint16_t)req[4] << 8) | req[5];
                via_save_pending = true;
            }
            break;
        }

        case 0x06: // dynamic_keymap_reset
        case 0x0A: // EEPROM reset
            keymap_defaults();
            via_save_pending = true;
            break;

        case 0x0C: // macro count
            resp[1] = 0;
            break;

        case 0x0D: // macro buffer size
            resp[1] = 0;
            resp[2] = 0;
            break;

        case 0x0E: // macro get buffer
        case 0x0F: // macro set buffer
        case 0x10: // macro reset
            break;

        case 0x11: // layer count
            resp[1] = VIA_LAYERS;
            break;

        case 0x12: { // dynamic keymap get buffer
            uint16_t offset = ((uint16_t)req[1] << 8) | req[2];
            uint8_t size = req[3];
            if (size > 28) size = 28;

            for (uint8_t i = 0; i < size; ++i) {
                resp[4 + i] = keymap_byte_at(offset + i);
            }
            break;
        }

        case 0x13: { // dynamic keymap set buffer
            uint16_t offset = ((uint16_t)req[1] << 8) | req[2];
            uint8_t size = req[3];
            if (size > 28) size = 28;

            for (uint8_t i = 0; i < size; ++i) {
                set_keymap_byte_at(offset + i, req[4 + i]);
            }
            via_save_pending = true;
            break;
        }

        default:
            resp[0] = 0xFF;
            break;
    }

}

/* ---------------- Lumi framing on the same Raw HID ---------------- */

#define LUMI_FLAG_START 0x01
#define LUMI_FLAG_END 0x02
#define LUMI_FLAG_RESPONSE 0x04
#define LUMI_HEADER_SIZE 7
#define LUMI_PAYLOAD_SIZE (RAW_SIZE - LUMI_HEADER_SIZE)
#define LUMI_MAX_MESSAGE 512

static char lumi_rx[LUMI_MAX_MESSAGE + 1];
static size_t lumi_rx_len;
static uint8_t lumi_seq;
static uint8_t lumi_fragment;
static bool lumi_active;

static void lumi_send_response(uint8_t sequence, const char *text)
{
    size_t len = strlen(text);
    size_t fragments = len == 0 ? 1 : (len + LUMI_PAYLOAD_SIZE - 1) / LUMI_PAYLOAD_SIZE;

    for (size_t f = 0; f < fragments; ++f) {
        uint8_t report[RAW_SIZE] = {0};
        size_t offset = f * LUMI_PAYLOAD_SIZE;
        size_t count = offset < len ? len - offset : 0;
        if (count > LUMI_PAYLOAD_SIZE) count = LUMI_PAYLOAD_SIZE;

        report[0] = LUMI_MAGIC0;
        report[1] = LUMI_MAGIC1;
        report[2] = LUMI_FRAME_VERSION;
        report[3] = LUMI_FLAG_RESPONSE;
        if (f == 0) report[3] |= LUMI_FLAG_START;
        if (f == fragments - 1) report[3] |= LUMI_FLAG_END;
        report[4] = sequence;
        report[5] = (uint8_t)f;
        report[6] = (uint8_t)count;

        if (count) {
            memcpy(&report[LUMI_HEADER_SIZE], text + offset, count);
        }

        for (int retry = 0; retry < 50; ++retry) {
            if (raw_send(report)) break;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}


/* ---------------- Firmware OTA over Lumi Raw HID ---------------- */

static esp_ota_handle_t fw_ota_handle;
static const esp_partition_t *fw_ota_partition;
static size_t fw_ota_expected;
static size_t fw_ota_written;
static bool fw_ota_active;

static void fw_ota_reset_state(bool abort_transfer)
{
    if (fw_ota_active && abort_transfer) {
        esp_ota_abort(fw_ota_handle);
    }

    fw_ota_handle = 0;
    fw_ota_partition = NULL;
    fw_ota_expected = 0;
    fw_ota_written = 0;
    fw_ota_active = false;
}

static void delayed_restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(450));
    esp_restart();
}

static void fw_ota_error(uint8_t sequence, const char *code)
{
    char response[96];
    snprintf(
        response,
        sizeof(response),
        "FWERR|%s|%u|%u",
        code,
        (unsigned)fw_ota_written,
        (unsigned)fw_ota_expected);
    lumi_send_response(sequence, response);
}

static void fw_ota_begin_command(uint8_t sequence, const char *command)
{
    const char *size_text = command + strlen("FWBEGIN|");
    char *end = NULL;
    unsigned long requested = strtoul(size_text, &end, 10);

    if (end == size_text || *end != '\0' || requested == 0) {
        fw_ota_error(sequence, "SIZE");
        return;
    }

    if (fw_ota_active) {
        fw_ota_reset_state(true);
    }

    const esp_partition_t *partition =
        esp_ota_get_next_update_partition(NULL);

    if (partition == NULL || requested > partition->size) {
        fw_ota_error(sequence, "PARTITION");
        return;
    }

    esp_ota_handle_t handle = 0;
    esp_err_t err =
        esp_ota_begin(partition, (size_t)requested, &handle);

    if (err != ESP_OK) {
        fw_ota_error(sequence, "BEGIN");
        return;
    }

    fw_ota_handle = handle;
    fw_ota_partition = partition;
    fw_ota_expected = (size_t)requested;
    fw_ota_written = 0;
    fw_ota_active = true;

    char response[96];
    snprintf(
        response,
        sizeof(response),
        "FWREADY|%u|%s",
        (unsigned)fw_ota_expected,
        partition->label);
    lumi_send_response(sequence, response);
}

static void fw_ota_chunk_command(uint8_t sequence, const char *command)
{
    if (!fw_ota_active) {
        fw_ota_error(sequence, "NOSESSION");
        return;
    }

    const char *offset_text = command + strlen("FWCHUNK|");
    char *end = NULL;
    unsigned long offset = strtoul(offset_text, &end, 10);

    if (end == offset_text || *end != '|') {
        fw_ota_error(sequence, "OFFSET");
        return;
    }

    const char *encoded = end + 1;
    size_t encoded_len = strlen(encoded);

    if (encoded_len == 0 || encoded_len > 320) {
        fw_ota_error(sequence, "CHUNK");
        return;
    }

    uint8_t decoded[192];
    size_t decoded_len = 0;

    int decode_result =
        mbedtls_base64_decode(
            decoded,
            sizeof(decoded),
            &decoded_len,
            (const unsigned char *)encoded,
            encoded_len);

    if (decode_result != 0 || decoded_len == 0) {
        fw_ota_error(sequence, "BASE64");
        return;
    }

    /*
     * If the host lost an ACK and retries the last chunk, simply return the
     * current committed position instead of writing duplicate bytes.
     */
    if ((size_t)offset < fw_ota_written) {
        char response[64];
        snprintf(
            response,
            sizeof(response),
            "FWACK|%u",
            (unsigned)fw_ota_written);
        lumi_send_response(sequence, response);
        return;
    }

    if ((size_t)offset != fw_ota_written ||
        fw_ota_written + decoded_len > fw_ota_expected) {
        fw_ota_error(sequence, "ORDER");
        return;
    }

    esp_err_t err =
        esp_ota_write(
            fw_ota_handle,
            decoded,
            decoded_len);

    if (err != ESP_OK) {
        fw_ota_error(sequence, "WRITE");
        return;
    }

    fw_ota_written += decoded_len;

    char response[64];
    snprintf(
        response,
        sizeof(response),
        "FWACK|%u",
        (unsigned)fw_ota_written);
    lumi_send_response(sequence, response);
}

static void fw_ota_status_command(uint8_t sequence)
{
    char response[80];
    snprintf(
        response,
        sizeof(response),
        "FWSTAT|%u|%u|%u",
        fw_ota_active ? 1U : 0U,
        (unsigned)fw_ota_written,
        (unsigned)fw_ota_expected);
    lumi_send_response(sequence, response);
}

static void fw_ota_end_command(uint8_t sequence)
{
    if (!fw_ota_active) {
        fw_ota_error(sequence, "NOSESSION");
        return;
    }

    if (fw_ota_written != fw_ota_expected) {
        fw_ota_error(sequence, "INCOMPLETE");
        return;
    }

    esp_err_t err = esp_ota_end(fw_ota_handle);
    if (err != ESP_OK) {
        fw_ota_reset_state(false);
        fw_ota_error(sequence, "VERIFY");
        return;
    }

    err = esp_ota_set_boot_partition(fw_ota_partition);
    if (err != ESP_OK) {
        fw_ota_reset_state(false);
        fw_ota_error(sequence, "BOOT");
        return;
    }

    size_t completed = fw_ota_written;
    fw_ota_reset_state(false);

    char response[64];
    snprintf(
        response,
        sizeof(response),
        "FWDONE|%u",
        (unsigned)completed);
    lumi_send_response(sequence, response);

    xTaskCreate(
        delayed_restart_task,
        "fw_restart",
        2048,
        NULL,
        12,
        NULL);
}

static void fw_ota_abort_command(uint8_t sequence)
{
    fw_ota_reset_state(true);
    lumi_send_response(sequence, "FWABORTED");
}

static void process_lumi_command(uint8_t sequence, const char *command)
{
    if (strcmp(command, "HELLO") == 0) {
        lumi_send_response(sequence, "LUMIPAD|3|FW=" FW_VERSION "|CAPS=MEM,FWOTA");
        return;
    }

    if (strcmp(command, "MEM") == 0) {
        /*
         * Keep MEM response simple for bring-up. The app only needs a valid
         * response; richer memory statistics can be restored later.
         */
        lumi_send_response(sequence, "MEM|0|4194304|0|327680");
        return;
    }

    if (strncmp(command, "FWBEGIN|", 8) == 0) {
        fw_ota_begin_command(sequence, command);
        return;
    }

    if (strncmp(command, "FWCHUNK|", 8) == 0) {
        fw_ota_chunk_command(sequence, command);
        return;
    }

    if (strcmp(command, "FWSTAT") == 0) {
        fw_ota_status_command(sequence);
        return;
    }

    if (strcmp(command, "FWEND") == 0) {
        fw_ota_end_command(sequence);
        return;
    }

    if (strcmp(command, "FWABORT") == 0) {
        fw_ota_abort_command(sequence);
        return;
    }

    if (strcmp(command, "SYS|RESTART") == 0) {
        lumi_send_response(sequence, "OK|RESTART");
        xTaskCreate(
            delayed_restart_task,
            "sys_restart",
            2048,
            NULL,
            12,
            NULL);
        return;
    }
}

static void process_lumi_frame(const uint8_t data[RAW_SIZE])
{
    uint8_t flags = data[3];
    uint8_t sequence = data[4];
    uint8_t fragment = data[5];
    uint8_t count = data[6];

    if ((flags & LUMI_FLAG_RESPONSE) || count > LUMI_PAYLOAD_SIZE) return;

    if (flags & LUMI_FLAG_START) {
        lumi_active = true;
        lumi_seq = sequence;
        lumi_fragment = 0;
        lumi_rx_len = 0;
    }

    if (!lumi_active || sequence != lumi_seq || fragment != lumi_fragment) {
        lumi_active = false;
        return;
    }

    if (lumi_rx_len + count > LUMI_MAX_MESSAGE) {
        lumi_active = false;
        return;
    }

    memcpy(lumi_rx + lumi_rx_len, data + LUMI_HEADER_SIZE, count);
    lumi_rx_len += count;
    lumi_fragment++;

    if (!(flags & LUMI_FLAG_END)) return;

    lumi_active = false;
    lumi_rx[lumi_rx_len] = '\0';
    process_lumi_command(sequence, lumi_rx);
}

static void process_raw_packet(const uint8_t data[RAW_SIZE])
{
    if (data[0] == LUMI_MAGIC0 &&
        data[1] == LUMI_MAGIC1 &&
        data[2] == LUMI_FRAME_VERSION) {
        process_lumi_frame(data);
    } else {
        uint8_t resp[RAW_SIZE];
        build_via_response(data, resp);

        for (int retry = 0; retry < 50; ++retry) {
            if (raw_send(resp)) break;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

/* ---------------- Bare-board keyboard test ---------------- */

static bool button_last = false;
static int64_t button_change_us;

static void keyboard_send_k1(bool pressed)
{
    uint8_t keys[6] = {0};

    if (pressed) {
        uint16_t kc = keymap[0][0][0];

        /*
         * For bring-up, support standard basic HID/QMK keycodes 0x04..0xA4.
         * K1 defaults to A (0x04).
         */
        if (kc >= 0x0004 && kc <= 0x00A4) {
            keys[0] = (uint8_t)kc;
        }
    }

    if (tud_hid_n_ready(ITF_NUM_KEYBOARD)) {
        tud_hid_n_keyboard_report(ITF_NUM_KEYBOARD, 0, 0, keys);
    }
}

static void button_task(void *arg)
{
    (void)arg;

    while (1) {
        bool pressed = gpio_get_level(BOOT_GPIO) == 0;
        int64_t now = esp_timer_get_time();

        if (pressed != button_last && now - button_change_us > 20000) {
            button_last = pressed;
            button_change_us = now;
            keyboard_send_k1(pressed);
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

static void usb_task(void *arg)
{
    (void)arg;
    raw_packet_t packet;
    raw_packet_t tx;

    while (1) {
        if (xQueueReceive(raw_tx_queue, &tx, 0) == pdTRUE) {
            if (!raw_send(tx.data)) {
                xQueueSendToFront(raw_tx_queue, &tx, 0);
            }
        }

        if (xQueueReceive(raw_queue, &packet, pdMS_TO_TICKS(5)) == pdTRUE) {
            process_raw_packet(packet.data);
        }

        if (via_save_pending) {
            via_save_pending = false;
            keymap_save();
        }
    }
}

static void led_task(void *arg)
{
    (void)arg;
    bool state = false;

    while (1) {
        state = !state;
        gpio_set_level(LED_GPIO, state);

        // Keep the convention used during bring-up:
        // slow = firmware alive but USB not mounted, fast = USB mounted.
        vTaskDelay(pdMS_TO_TICKS(tud_mounted() ? 250 : 1000));
    }
}

void app_main(void)
{
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(nvs_err);
    }

    keymap_load();

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BOOT_GPIO) | (1ULL << LED_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    gpio_set_direction(BOOT_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BOOT_GPIO, GPIO_PULLUP_ONLY);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    raw_queue = xQueueCreate(16, sizeof(raw_packet_t));
    raw_tx_queue = xQueueCreate(16, sizeof(raw_packet_t));
    ESP_ERROR_CHECK(
        (raw_queue && raw_tx_queue) ? ESP_OK : ESP_ERR_NO_MEM);

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.device = &device_descriptor;
    tusb_cfg.descriptor.full_speed_config = configuration_descriptor;
    tusb_cfg.descriptor.string = string_descriptor;
    tusb_cfg.descriptor.string_count = sizeof(string_descriptor) / sizeof(string_descriptor[0]);

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    ESP_LOGI(TAG, "PIXEL PRO %s started", FW_VERSION);

    xTaskCreate(usb_task, "usb_raw", 4096, NULL, 10, NULL);
    xTaskCreate(button_task, "button", 2048, NULL, 5, NULL);
    xTaskCreate(led_task, "led", 2048, NULL, 3, NULL);
}

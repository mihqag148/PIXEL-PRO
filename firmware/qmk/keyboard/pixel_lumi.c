#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "raw_hid.h"

#define LUMI_MAGIC0 'L'
#define LUMI_MAGIC1 'Q'
#define LUMI_VERSION 1

#define LUMI_FLAG_START    0x01
#define LUMI_FLAG_END      0x02
#define LUMI_FLAG_RESPONSE 0x04

#define LUMI_HEADER_SIZE 7
#define LUMI_PAYLOAD_SIZE (32 - LUMI_HEADER_SIZE)
#define LUMI_MAX_MESSAGE 256

static char rx[LUMI_MAX_MESSAGE + 1];
static uint16_t rx_len;
static uint8_t rx_seq;
static uint8_t rx_fragment;
static bool rx_active;

static void send_response(uint8_t sequence, const char *text) {
    uint16_t len = (uint16_t)strlen(text);
    uint8_t fragments = len == 0 ? 1 : (uint8_t)((len + LUMI_PAYLOAD_SIZE - 1) / LUMI_PAYLOAD_SIZE);

    for (uint8_t f = 0; f < fragments; ++f) {
        uint8_t report[32] = {0};
        uint16_t offset = (uint16_t)f * LUMI_PAYLOAD_SIZE;
        uint8_t count = offset < len ? (uint8_t)(len - offset) : 0;

        if (count > LUMI_PAYLOAD_SIZE) {
            count = LUMI_PAYLOAD_SIZE;
        }

        report[0] = LUMI_MAGIC0;
        report[1] = LUMI_MAGIC1;
        report[2] = LUMI_VERSION;
        report[3] = LUMI_FLAG_RESPONSE;
        if (f == 0) report[3] |= LUMI_FLAG_START;
        if (f == fragments - 1) report[3] |= LUMI_FLAG_END;
        report[4] = sequence;
        report[5] = f;
        report[6] = count;

        if (count) {
            memcpy(report + LUMI_HEADER_SIZE, text + offset, count);
        }

        raw_hid_send(report, sizeof(report));
    }
}

static void process_command(uint8_t sequence, const char *command) {
    if (strcmp(command, "HELLO") == 0) {
        /*
         * Keep bring-up HELLO within one 25-byte Lumi payload. The TinyUSB
         * callback path can safely send one immediate reply; larger feature
         * replies will be restored after the Raw HID path is confirmed on
         * physical hardware.
         */
        send_response(sequence, "LUMIPAD|3|FW=0.2.1");
        return;
    }

    send_response(sequence, "ERR|UNSUPPORTED");
}

/*
 * Called by a small dispatcher patch in QMK's real VIA core. Returning true
 * means the packet belongs to Lumi and must not be interpreted as a VIA
 * command. All normal VIA packets continue through quantum/via.c unchanged.
 */
bool pixel_lumi_receive(uint8_t *data, uint8_t length) {
    if (length != 32 ||
        data[0] != LUMI_MAGIC0 ||
        data[1] != LUMI_MAGIC1 ||
        data[2] != LUMI_VERSION) {
        return false;
    }

    uint8_t flags = data[3];
    uint8_t sequence = data[4];
    uint8_t fragment = data[5];
    uint8_t count = data[6];

    if ((flags & LUMI_FLAG_RESPONSE) || count > LUMI_PAYLOAD_SIZE) {
        return true;
    }

    if (flags & LUMI_FLAG_START) {
        rx_active = true;
        rx_seq = sequence;
        rx_fragment = 0;
        rx_len = 0;
    }

    if (!rx_active || sequence != rx_seq || fragment != rx_fragment) {
        rx_active = false;
        return true;
    }

    if ((uint16_t)(rx_len + count) > LUMI_MAX_MESSAGE) {
        rx_active = false;
        return true;
    }

    memcpy(rx + rx_len, data + LUMI_HEADER_SIZE, count);
    rx_len += count;
    rx_fragment++;

    if (!(flags & LUMI_FLAG_END)) {
        return true;
    }

    rx_active = false;
    rx[rx_len] = '\0';
    process_command(sequence, rx);
    return true;
}

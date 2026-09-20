#include "pixel_pro.h"

#include <stdbool.h>
#include <stdint.h>

#include "gpio.h"
#include "matrix.h"
#include "wait.h"

/*
 * Existing PIXEL PRO wiring:
 * ROW0 GPIO13, ROW1 GPIO14
 * COL0..COL3 GPIO33..GPIO36
 * EC11 push GPIO39 -> GND
 *
 * Diode cathodes/stripes point toward ROW, so each row is driven low while
 * columns are read with pull-ups.
 */
static const pin_t row_pins[2] = {13, 14};
static const pin_t col_pins[4] = {33, 34, 35, 36};
static const pin_t encoder_sw_pin = 39;

static matrix_row_t matrix_state[MATRIX_ROWS];

matrix_row_t matrix_get_row(uint8_t row) {
    if (row >= MATRIX_ROWS) {
        return 0;
    }
    return matrix_state[row];
}

void matrix_print(void) {
}

__attribute__((weak)) void matrix_init_kb(void) {
    matrix_init_user();
}

__attribute__((weak)) void matrix_init_user(void) {
}

void matrix_init(void) {
    for (uint8_t r = 0; r < 2; ++r) {
        setPinInputHigh(row_pins[r]);
    }

    for (uint8_t c = 0; c < 4; ++c) {
        setPinInputHigh(col_pins[c]);
    }

    setPinInputHigh(encoder_sw_pin);

    for (uint8_t r = 0; r < MATRIX_ROWS; ++r) {
        matrix_state[r] = 0;
    }

    matrix_init_quantum();
}

__attribute__((weak)) void matrix_scan_kb(void) {
    matrix_scan_user();
}

__attribute__((weak)) void matrix_scan_user(void) {
}

uint8_t matrix_scan(void) {
    matrix_row_t next[MATRIX_ROWS] = {0};

    for (uint8_t r = 0; r < 2; ++r) {
        setPinOutput(row_pins[r]);
        writePinLow(row_pins[r]);
        wait_us(5);

        for (uint8_t c = 0; c < 4; ++c) {
            if (!readPin(col_pins[c])) {
                next[r] |= ((matrix_row_t)1 << c);
            }
        }

        setPinInputHigh(row_pins[r]);
    }

    if (!readPin(encoder_sw_pin)) {
        next[2] |= 1;
    }

    bool changed = false;
    for (uint8_t r = 0; r < MATRIX_ROWS; ++r) {
        if (next[r] != matrix_state[r]) {
            matrix_state[r] = next[r];
            changed = true;
        }
    }

    return changed ? 1 : 0;
}

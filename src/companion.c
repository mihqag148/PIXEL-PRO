/* SPDX-License-Identifier: MIT */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/usb/class/usb_hid.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/usb.h>
#include <stdio.h>
#include <string.h>

/* HID_0 belongs to ZMK. HID_1 is an independent vendor collection. */
static const struct device *vendor;
static const uint8_t descriptor[] = {
    0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01,
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x40,
    0x09, 0x01, 0x81, 0x02,
    0x09, 0x02, 0xB1, 0x02,
    0xC0
};
K_MSGQ_DEFINE(events, 64, 32, 4);
K_SEM_DEFINE(tx_ready, 1, 1);
static atomic_t key_state;
static atomic_t nav_state;
static atomic_t dropped;

static int get_report(const struct device *dev, struct usb_setup_packet *setup,
                      int32_t *len, uint8_t **data)
{
    static uint8_t reply[64];
    if ((setup->wValue >> 8) != 3 || (setup->wValue & 0xff) != 0) {
        return -ENOTSUP;
    }
    memset(reply, 0, sizeof(reply));
    snprintf(reply, sizeof(reply), "PIXELPRO|ZMK|0.3.0|KEYS=%02X|NAV=%X|DROP=%u",
             (unsigned)atomic_get(&key_state), (unsigned)atomic_get(&nav_state),
             (unsigned)atomic_get(&dropped));
    *data = reply;
    *len = MIN(*len, sizeof(reply));
    return 0;
}
static void in_ready(const struct device *dev) { k_sem_give(&tx_ready); }
static const struct hid_ops ops = { .get_report = get_report, .int_in_ready = in_ready };

static void publish(const char *kind, unsigned key, bool down)
{
    uint8_t report[64] = {0};
    snprintf(report, sizeof(report), "%s|%u|%s", kind, key, down ? "DOWN" : "UP");
    if (k_msgq_put(&events, report, K_NO_WAIT)) { atomic_inc(&dropped); }
}
static int position_listener(const zmk_event_t *eh)
{
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev && ev->position < 8) {
        if (ev->state) { atomic_set_bit(&key_state, ev->position); }
        else { atomic_clear_bit(&key_state, ev->position); }
        publish("KEY", ev->position + 1, ev->state);
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(pixel_companion, position_listener);
ZMK_SUBSCRIPTION(pixel_companion, zmk_position_state_changed);

static int companion_init(void)
{
    vendor = device_get_binding("HID_1");
    if (!vendor) { return -ENODEV; }
    usb_hid_register_device(vendor, descriptor, sizeof(descriptor), &ops);
    return usb_hid_init(vendor);
}
SYS_INIT(companion_init, APPLICATION, 50);

static void tx_thread(void *a, void *b, void *c)
{
    uint8_t report[64];
    while (true) {
        k_msgq_get(&events, report, K_FOREVER);
        if (!vendor || !zmk_usb_is_hid_ready()) { continue; }
        if (k_sem_take(&tx_ready, K_MSEC(100))) {
            atomic_inc(&dropped);
            /* A bus reset may cancel a pending IN without a completion. */
            k_sem_give(&tx_ready);
            continue;
        }
        if (hid_int_ep_write(vendor, report, sizeof(report), NULL)) {
            k_sem_give(&tx_ready);
            atomic_inc(&dropped);
        }
    }
}
K_THREAD_DEFINE(pixel_tx, 1536, tx_thread, NULL, NULL, NULL, 8, 0, 0);

/* Optional three-way switch: GPIO9=left, GPIO10=press, GPIO11=right.
 * Unwired contacts stay high through pull-ups and never emit events. */
static void nav_thread(void *a, void *b, void *c)
{
    const struct device *gpio = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    uint8_t stable = 0, previous = 0;
    unsigned settled = 0;
    if (!device_is_ready(gpio)) { return; }
    for (int pin = 9; pin <= 11; pin++) {
        if (gpio_pin_configure(gpio, pin, GPIO_INPUT | GPIO_PULL_UP)) { return; }
    }
    while (true) {
        uint8_t sample = 0;
        for (int n = 0; n < 3; n++) {
            if (gpio_pin_get(gpio, 9 + n) == 0) { sample |= BIT(n); }
        }
        if (sample != previous) { previous = sample; settled = 0; }
        else if (settled < 5) { settled++; }
        if (settled == 5 && stable != sample) {
            uint8_t changed = stable ^ sample;
            stable = sample;
            atomic_set(&nav_state, stable);
            for (int n = 0; n < 3; n++) {
                if (changed & BIT(n)) { publish("NAV", n + 1, stable & BIT(n)); }
            }
        }
        k_msleep(1);
    }
}
K_THREAD_DEFINE(pixel_nav, 1024, nav_thread, NULL, NULL, NULL, 9, 0, 100);

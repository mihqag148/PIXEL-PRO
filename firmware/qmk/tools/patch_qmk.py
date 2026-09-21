#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1]).resolve()

tiny = root / "tmk_core/protocol/tinyusb/main.c"
text = tiny.read_text(encoding="utf-8")

include_anchor = '#include "host_driver.h"\n'
include_patch = '''#include "host_driver.h"
#ifdef RAW_ENABLE
#    include "raw_hid.h"
#endif
'''
if include_anchor not in text:
    raise SystemExit("TinyUSB include anchor not found")
text = text.replace(include_anchor, include_patch, 1)

if "#include <string.h>" not in text:
    text = text.replace("#include <stdarg.h>\n", "#include <stdarg.h>\n#include <string.h>\n", 1)

callback_anchor = '''            break;
    }
}

uint16_t const* tud_descriptor_string_cb'''
callback_patch = '''            break;

#ifdef RAW_ENABLE
        case RAW_INTERFACE:
            if (bufsize == RAW_EPSIZE) {
                uint8_t raw[RAW_EPSIZE];
                memcpy(raw, buffer, RAW_EPSIZE);
                raw_hid_receive(raw, RAW_EPSIZE);
            }
            break;
#endif
    }
}

uint16_t const* tud_descriptor_string_cb'''
if callback_anchor not in text:
    raise SystemExit("TinyUSB set-report callback anchor not found")
text = text.replace(callback_anchor, callback_patch, 1)

send_anchor = '''#ifdef CONSOLE_ENABLE
int8_t sendchar(uint8_t c) {'''
send_patch = '''#ifdef RAW_ENABLE
void raw_hid_send(uint8_t *data, uint8_t length) {
    if (length != RAW_EPSIZE) {
        return;
    }

    uint8_t itf_index = tud_hid_itf_num_to_index(RAW_INTERFACE);
    if (!wait_for_hid_ready(itf_index)) {
        return;
    }

    tud_hid_n_report(itf_index, 0, data, RAW_EPSIZE);
}

void raw_hid_task(void) {
}
#endif

#ifdef CONSOLE_ENABLE
int8_t sendchar(uint8_t c) {'''
if send_anchor not in text:
    raise SystemExit("TinyUSB raw-send anchor not found")
text = text.replace(send_anchor, send_patch, 1)
tiny.write_text(text, encoding="utf-8")

# The old ESP32-S2 QMK fork numbers the Raw HID IN endpoint before OUT by
# default (IN=EP2, OUT=EP3). PIXEL PRO hardware bring-up already proved the
# stable ESP32-S2/Windows layout is OUT=EP2 and IN=EP3. Keep keyboard IN on
# EP1 and the shared consumer endpoint on EP4.
usb_desc = root / "tmk_core/protocol/usb_descriptor.h"
text = usb_desc.read_text(encoding="utf-8")
endpoint_anchor = '''#ifdef RAW_ENABLE
    RAW_IN_EPNUM,
#    if STM32_USB_USE_OTG1
#        define RAW_OUT_EPNUM RAW_IN_EPNUM
#    else
    RAW_OUT_EPNUM,
#    endif
#endif
'''
endpoint_patch = '''#ifdef RAW_ENABLE
#    ifdef PROTOCOL_TINYUSB
    RAW_OUT_EPNUM,
    RAW_IN_EPNUM,
#    else
    RAW_IN_EPNUM,
#        if STM32_USB_USE_OTG1
#            define RAW_OUT_EPNUM RAW_IN_EPNUM
#        else
    RAW_OUT_EPNUM,
#        endif
#    endif
#endif
'''
if endpoint_anchor not in text:
    raise SystemExit("Raw HID endpoint enum anchor not found")
text = text.replace(endpoint_anchor, endpoint_patch, 1)
usb_desc.write_text(text, encoding="utf-8")

via = root / "quantum/via.c"
text = via.read_text(encoding="utf-8")

via_anchor = '''void raw_hid_receive(uint8_t *data, uint8_t length) {
    uint8_t *command_id'''
via_patch = '''#ifdef PIXEL_LUMI_ENABLE
extern bool pixel_lumi_receive(uint8_t *data, uint8_t length);
#endif

void raw_hid_receive(uint8_t *data, uint8_t length) {
#ifdef PIXEL_LUMI_ENABLE
    if (pixel_lumi_receive(data, length)) {
        return;
    }
#endif

    uint8_t *command_id'''
if via_anchor not in text:
    raise SystemExit("VIA dispatcher anchor not found")
text = text.replace(via_anchor, via_patch, 1)
via.write_text(text, encoding="utf-8")

print("Patched ESP32-S2 QMK TinyUSB Raw HID endpoints + Lumi dispatcher")

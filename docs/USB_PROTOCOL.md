# PIXEL PRO companion protocol, version 1

Firmware 0.3.0. Product PIXEL PRO ZMK. Manufacturer LumiPad Development.
VID 0x1209 / PID 0x0001 is a development-only test identity, not an allocated
production identity. Obtain an allocated PID before distributing hardware.
Bring-up serial PIXELPRO-S2-DEV-001 is fixed; use one prototype at a time.

Two independent HID interfaces: HID_0 is the ZMK keyboard; HID_1 is vendor
usage page 0xFF00, usage 1. Keyboard functionality does not require LumiPad.
Vendor reports have 64 data bytes, no report IDs (Windows/HidSharp prepends
the mandatory zero byte, so API buffers are 65 bytes).

GET_REPORT FEATURE (report ID 0) is the HELLO request. It returns a
zero-padded ASCII string:
PIXELPRO|ZMK|0.3.0|KEYS=00|NAV=0|DROP=0

KEYS and NAV are hexadecimal pressed-bit masks. DROP counts reports lost
to backpressure. Poll FEATURE to resynchronize after loss or reconnect.
Interrupt IN reports: KEY|1|DOWN, KEY|1|UP ... KEY|8|UP;
NAV|1|DOWN ... NAV|3|UP. Navigation order: left, press, right.
No interrupt OUT endpoint or output report is defined in phase 1.
Feature requests use endpoint zero. Reject other products/protocols.

Transport runs on Zephyr's native legacy USB stack and adapted DesignWare
controller driver. No alternate USB stack is linked.

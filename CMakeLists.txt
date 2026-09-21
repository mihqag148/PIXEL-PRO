zephyr_library()
# This external module consumes ZMK's internal event/USB headers. ZMK adds
# app/include only to its own application target, so expose it explicitly to
# this module library as well.
zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
zephyr_library_sources_ifdef(CONFIG_PIXEL_PRO_USB drivers/usb_dc_pixel_s2.c src/companion.c)
zephyr_library_include_directories(
    ${ZEPHYR_BASE}/drivers/usb/device
    ${ZEPHYR_BASE}/drivers/usb/common
)

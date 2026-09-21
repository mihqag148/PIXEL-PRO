zephyr_library()
zephyr_library_sources_ifdef(CONFIG_PIXEL_PRO_USB drivers/usb_dc_pixel_s2.c src/companion.c)
zephyr_library_include_directories(${ZEPHYR_BASE}/drivers/usb/device)

idf_component_register(
    SRCS "pixel_pro_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_gpio nvs_flash
)

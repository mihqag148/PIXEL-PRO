set(QMK_IDF_REQUIRES
    driver
    esp_timer
    esp_system
    esp_hw_support
    freertos
    log
    vfs
    fatfs
    wear_levelling
)

idf_component_register(
    SRCS "stub.c"
    INCLUDE_DIRS "."
    REQUIRES ${QMK_IDF_REQUIRES}
)

set(QMK_ARCHIVE "${CMAKE_CURRENT_LIST_DIR}/qmk_core.a")
if(EXISTS "${QMK_ARCHIVE}")
    add_prebuilt_library(
        pixel_pro_qmk_core
        "${QMK_ARCHIVE}"
        REQUIRES ${QMK_IDF_REQUIRES}
    )

    target_link_libraries(
        ${COMPONENT_LIB}
        INTERFACE
        "-Wl,--whole-archive"
        pixel_pro_qmk_core
        "-Wl,--no-whole-archive"
    )
endif()

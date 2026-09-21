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

    # Do NOT wrap the QMK target in a broad --whole-archive region. ESP-IDF
    # expands transitive component dependencies around component link
    # interfaces; broad whole-archive forced those same IDF archives into the
    # image more than once. app_main() is referenced by ESP-IDF startup, so
    # normal static archive resolution pulls the QMK entry object and then its
    # dependent QMK objects naturally.
    target_link_libraries(
        ${COMPONENT_LIB}
        INTERFACE
        pixel_pro_qmk_core
    )
endif()

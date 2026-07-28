# Sources are split into a shared set and a per-board set keyed on IDF_TARGET:
# esp32s3 = StopWatch wearable (round AMOLED, all apps), esp32 = M5Paper v1.1
# (e-ink, Aqua Timer only). New files are picked up by the globs, but CMake must
# reconfigure to notice them (touch a CMakeLists.txt or idf.py fullclean).

file(GLOB_RECURSE SHARED_SRCS
    "apps/common/*.c"
    "apps/common/*.cc"
    "apps/common/*.cpp"
    "apps/app_aqua_timer/model/*.cpp"
    "assets/*.c"
    "assets/*.cc"
    "assets/*.cpp"
    "hal/utils/*.c"
    "hal/utils/*.cc"
    "hal/utils/*.cpp"
)
list(APPEND SHARED_SRCS
    "apps/app_aqua_timer/app_aqua_timer.cpp"
    "hal/hal_common.cpp"
    "hal/hal_fs.cpp"
)

if("${IDF_TARGET}" STREQUAL "esp32")
    # M5Paper: Aqua Timer only, e-ink views, no audio/IMU/PMIC/IOE hardware
    file(GLOB_RECURSE BOARD_SRCS
        "apps/app_aqua_timer/view_epd/*.cpp"
        "hal/boards/m5paper/*.cpp"
    )
    set(BOARD_DEFS BOARD_M5PAPER=1)
    set(BOARD_EMBED_FILES "")
    set(BOARD_EMBED_TXTFILES
        "hal/utils/config_ap/assets/badge_config_ap.html"
        "hal/utils/net/assets/aqua_config_ap.html"
    )
else()
    # StopWatch: every app, AMOLED views, full hardware stack
    file(GLOB_RECURSE BOARD_SRCS
        "apps/*.c"
        "apps/*.cc"
        "apps/*.cpp"
        "hal/boards/stopwatch/*.cpp"
        "hal/drivers/*.c"
        "hal/drivers/*.cc"
        "hal/drivers/*.cpp"
    )
    # Everything already in SHARED_SRCS, plus the e-ink views, must not double in
    list(FILTER BOARD_SRCS EXCLUDE REGEX "apps/common/")
    list(FILTER BOARD_SRCS EXCLUDE REGEX "apps/app_aqua_timer/(model|view_epd)/")
    list(FILTER BOARD_SRCS EXCLUDE REGEX "apps/app_aqua_timer/app_aqua_timer.cpp")
    set(BOARD_DEFS BOARD_STOPWATCH=1)
    set(BOARD_EMBED_FILES "assets/sfx/boot_sfx.bin")
    set(BOARD_EMBED_TXTFILES
        "hal/utils/config_ap/assets/badge_config_ap.html"
        "hal/utils/net/assets/aqua_config_ap.html"
    )
endif()

set(MY_INCS
    "."
)

idf_component_register(
    SRCS
        "main.cpp"
        ${SHARED_SRCS}
        ${BOARD_SRCS}
    INCLUDE_DIRS
        ${MY_INCS}
    EMBED_FILES
        ${BOARD_EMBED_FILES}
    EMBED_TXTFILES
        ${BOARD_EMBED_TXTFILES}
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE ${BOARD_DEFS})

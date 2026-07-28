# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Board = IDF target: esp32s3 -> StopWatch wearable (default), esp32 -> M5Paper.
# The S3-only vendored components must be excluded on esp32 or their sources
# fail to compile for the older core.
if(NOT DEFINED IDF_TARGET AND DEFINED ENV{IDF_TARGET})
    set(IDF_TARGET $ENV{IDF_TARGET})
endif()
if("${IDF_TARGET}" STREQUAL "esp32")
    set(EXCLUDE_COMPONENTS M5IOE1 M5PM1 BMI270_BMM150_Sensor)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(StopWatch-UserDemo)

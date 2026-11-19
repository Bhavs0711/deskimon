cmake_minimum_required(VERSION 3.16)

# Deskimon firmware - Waveshare ESP32-S3-Touch-LCD-1.85 (ESP-IDF >= 5.3.1)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(deskimon)

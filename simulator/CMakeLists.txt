cmake_minimum_required(VERSION 3.16)
project(deskimon_simulator C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Find SDL2
find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

# LVGL configuration
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE STRING "" FORCE)
add_compile_definitions(LV_CONF_INCLUDE_SIMPLE)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/esp_stubs
    ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/components/lvgl
    ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/components/dsk_face/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/components/dsk_board/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/components/dsk_app/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lv_drivers
)

# Add compile definitions for sdkconfig
add_compile_definitions(
    CONFIG_DSK_FACE_RING=1
    CONFIG_DSK_FACE_FPS=30
    CONFIG_DSK_LVGL_BUF_LINES=40
)

# LVGL source files
file(GLOB_RECURSE LVGL_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/../firmware/components/lvgl/src/*.c"
)

# SDL2 driver for LVGL
set(SDL_DRIVER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/lv_drivers/sdl/sdl.c
)

# Deskimon UI components (face, overlay, FSM, emotion)
set(DESKIMON_UI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/components/dsk_face/dsk_face.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/components/dsk_face/dsk_overlay.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/components/dsk_face/dsk_fsm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/components/dsk_face/dsk_emotion.c
)

# Simulator-specific sources
set(SIMULATOR_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/hal_sim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/http_control.c
)

# Create executable
add_executable(deskimon_sim
    ${LVGL_SOURCES}
    ${SDL_DRIVER_SOURCES}
    ${DESKIMON_UI_SOURCES}
    ${SIMULATOR_SOURCES}
)

# Link SDL2
target_link_libraries(deskimon_sim ${SDL2_LIBRARIES} m pthread)

# Compiler flags
target_compile_options(deskimon_sim PRIVATE
    -Wall
    -Wno-unused-function
    -Wno-unused-variable
    -O2
)

# Add definitions
target_compile_definitions(deskimon_sim PRIVATE
    SIMULATOR_BUILD=1
)

message(STATUS "Deskimon Simulator configured")
message(STATUS "  SDL2: ${SDL2_LIBRARIES}")
message(STATUS "  LVGL sources: ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/components/lvgl")

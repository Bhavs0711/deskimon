cmake_minimum_required(VERSION 3.16)
project(DeskimonSimulator C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

set(LVGL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/components/lvgl")
set(FACE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/components/dsk_face")

file(GLOB_RECURSE LVGL_SOURCES "${LVGL_DIR}/src/*.c")

add_executable(deskimon_sim
    src/main.c
    src/platform_shim.c
    "${FACE_DIR}/dsk_face.c"
    "${FACE_DIR}/dsk_emotion.c"
    ${LVGL_SOURCES}
)

target_include_directories(deskimon_sim PRIVATE
    include
    "${FACE_DIR}/include"
    "${LVGL_DIR}"
    "${LVGL_DIR}/src"
    ${SDL2_INCLUDE_DIRS}
)

target_compile_definitions(deskimon_sim PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    LV_LVGL_H_INCLUDE_SIMPLE
)

target_compile_options(deskimon_sim PRIVATE ${SDL2_CFLAGS_OTHER})
target_link_libraries(deskimon_sim PRIVATE ${SDL2_LIBRARIES} m)

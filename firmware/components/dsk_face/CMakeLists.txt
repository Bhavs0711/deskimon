idf_component_register(
    SRCS "dsk_emotion.c" "dsk_fsm.c" "dsk_face.c" "dsk_overlay.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl dsk_board esp_timer
)

idf_component_register(
    SRCS "main.c" "dsk_speaker_test.c"
    INCLUDE_DIRS "."
    REQUIRES dsk_board dsk_face dsk_app nvs_flash
)

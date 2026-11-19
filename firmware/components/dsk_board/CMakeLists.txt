idf_component_register(
    SRCS "dsk_board.c" "dsk_imu.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_lcd esp_timer lvgl esp_lcd_st77916 esp_lcd_touch esp_lcd_touch_cst816s
)

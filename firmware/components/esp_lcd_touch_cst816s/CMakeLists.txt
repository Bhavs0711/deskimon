set(REQ "driver" "esp_lcd")

idf_component_register(
    SRCS "esp_lcd_touch_cst816s.c"
    INCLUDE_DIRS "include"
    REQUIRES ${REQ}
    PRIV_REQUIRES esp_lcd_touch
)

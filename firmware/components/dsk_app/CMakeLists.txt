idf_component_register(
    SRCS "dsk_app.c" "dsk_net.c" "dsk_ble.c" "dsk_audio.c" "dsk_store.c" "dsk_auth.c" "dsk_wakeword.c"
    INCLUDE_DIRS "include"
    REQUIRES dsk_board dsk_face json nvs_flash esp_wifi esp_netif esp_event lwip mbedtls bt driver esp_timer
)

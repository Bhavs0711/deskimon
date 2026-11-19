#pragma once
#include <stdbool.h>
#include "esp_err.h"
typedef void (*dsk_ble_line_cb_t)(const char *line);
typedef void (*dsk_ble_cfg_cb_t)(const char *json);   /* provisioning payload (only while pairing) */
typedef void (*dsk_ble_link_cb_t)(bool connected);
esp_err_t dsk_ble_start(dsk_ble_line_cb_t on_line, dsk_ble_cfg_cb_t on_cfg, dsk_ble_link_cb_t on_link);
bool dsk_ble_connected(void);
bool dsk_ble_send_line(const char *line);          /* chunked notify, appends '\n' */
void dsk_ble_set_pairing(bool on);
bool dsk_ble_pairing(void);

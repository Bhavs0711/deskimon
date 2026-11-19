#pragma once
#include <stdbool.h>
#include "esp_err.h"
/* Wi-Fi station + UDP discovery + TCP client to the Mac Agent. Lines are newline-terminated JSON. */
typedef void (*dsk_line_cb_t)(const char *line);
typedef void (*dsk_link_cb_t)(bool tcp_up);
esp_err_t dsk_net_start(dsk_line_cb_t on_line, dsk_link_cb_t on_link);
bool dsk_net_wifi_up(void);
bool dsk_net_tcp_up(void);
bool dsk_net_send_line(const char *line);          /* appends '\n' */
void dsk_net_reconfigure(void);                    /* re-read credentials from NVS and reconnect */
void dsk_net_force_reconnect(void);

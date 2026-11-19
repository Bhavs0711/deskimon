#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
esp_err_t dsk_store_init(void);
bool dsk_store_get(const char *key, char *out, size_t len);   /* false if missing */
esp_err_t dsk_store_set(const char *key, const char *val);
esp_err_t dsk_store_erase_all(void);

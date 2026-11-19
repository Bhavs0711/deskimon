#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "dsk_store.h"
#define NS "deskimon"
esp_err_t dsk_store_init(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); e = nvs_flash_init(); }
    return e;
}
bool dsk_store_get(const char *key, char *out, size_t len)
{
    nvs_handle_t h; if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t n = len; esp_err_t e = nvs_get_str(h, key, out, &n); nvs_close(h); return e == ESP_OK;
}
esp_err_t dsk_store_set(const char *key, const char *val)
{
    nvs_handle_t h; esp_err_t e = nvs_open(NS, NVS_READWRITE, &h); if (e != ESP_OK) return e;
    e = nvs_set_str(h, key, val); if (e == ESP_OK) e = nvs_commit(h); nvs_close(h); return e;
}
esp_err_t dsk_store_erase_all(void)
{
    nvs_handle_t h; esp_err_t e = nvs_open(NS, NVS_READWRITE, &h); if (e != ESP_OK) return e;
    e = nvs_erase_all(h); if (e == ESP_OK) e = nvs_commit(h); nvs_close(h); return e;
}

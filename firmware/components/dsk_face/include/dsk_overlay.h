#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DSK_OVL_INFO = 0,
    DSK_OVL_MEETING,
    DSK_OVL_REMINDER,
    DSK_OVL_ALARM,
    DSK_OVL_EMAIL,
    DSK_OVL_STATUS,
    DSK_OVL_TIME,
    DSK_OVL_PROCESSING,
    DSK_OVL_PAIRING,
    DSK_OVL_MENU,
    DSK_OVL_KIND_COUNT
} dsk_overlay_kind_t;

typedef struct {
    bool wifi;          /* station connected */
    bool ble;           /* BLE central (the Mac) connected */
    bool mac;           /* authenticated session with the Mac Agent */
    bool pairing;
} dsk_status_t;

esp_err_t dsk_overlay_init(void);

/** Show a temporary card. duration_ms == 0 keeps it until dsk_overlay_hide(). Thread-safe. */
void dsk_overlay_show(dsk_overlay_kind_t kind, const char *title, const char *subtitle, uint32_t duration_ms);
void dsk_overlay_hide(void);
bool dsk_overlay_visible(void);
dsk_overlay_kind_t dsk_overlay_kind(void);

/** Status icons along the top edge (Wi-Fi / Mac / BLE). Thread-safe. */
void dsk_overlay_set_status(const dsk_status_t *st);

/** Small always-on clock under the face. epoch == 0 hides it. Thread-safe. */
void dsk_overlay_set_clock(const char *hhmm);

#ifdef __cplusplus
}
#endif

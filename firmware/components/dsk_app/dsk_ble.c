/*
 * BLE GATT peripheral (NimBLE). Used for pairing/provisioning, status and fallback commands.
 * Service  6e5d0001-b5a3-f393-e0a9-e50e24dcca9e
 *   RX  ...0002  write / write-no-rsp : newline-delimited JSON from the Mac
 *   TX  ...0003  notify               : newline-delimited JSON to the Mac (chunked to MTU-3)
 *   CFG ...0004  write                : provisioning JSON {"ssid","pass","token","host"} (pairing mode only)
 * HARDWARE TEST REQUIRED.
 */
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "dsk_ble.h"

static const char *TAG = "dsk_ble";
#define UUID(n) BLE_UUID128_INIT(0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,(n),0x00,0x5d,0x6e)
static const ble_uuid128_t u_svc = UUID(0x01), u_rx = UUID(0x02), u_tx = UUID(0x03), u_cfg = UUID(0x04);

static dsk_ble_line_cb_t s_on_line;
static dsk_ble_cfg_cb_t s_on_cfg;
static dsk_ble_link_cb_t s_on_link;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE, s_tx_handle, s_mtu = 23;
static bool s_notify, s_pairing;
static uint8_t s_own_addr;
static char s_rx[1024]; static size_t s_rx_len;

static void advertise(void);

static int rx_access(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    uint16_t n = OS_MBUF_PKTLEN(ctxt->om);
    uint8_t tmp[256]; if (n > sizeof tmp) n = sizeof tmp;
    uint16_t out = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, tmp, n, &out) != 0) return BLE_ATT_ERR_UNLIKELY;
    for (uint16_t i = 0; i < out; i++) {
        char c = (char)tmp[i];
        if (c == '\n' || c == '\r') {
            if (s_rx_len && s_on_line) { s_rx[s_rx_len] = 0; s_on_line(s_rx); }
            s_rx_len = 0;
        } else if (s_rx_len < sizeof s_rx - 1) s_rx[s_rx_len++] = c;
        else s_rx_len = 0;
    }
    /* A single write that carries a full JSON object without newline (common from simple clients) */
    if (s_rx_len && s_rx[0] == '{' && s_rx[s_rx_len - 1] == '}') {
        int depth = 0; for (size_t i = 0; i < s_rx_len; i++) { if (s_rx[i] == '{') depth++; else if (s_rx[i] == '}') depth--; }
        if (depth == 0 && s_on_line) { s_rx[s_rx_len] = 0; s_on_line(s_rx); s_rx_len = 0; }
    }
    return 0;
}

static int cfg_access(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    if (!s_pairing) { ESP_LOGW(TAG, "provisioning rejected: not in pairing mode"); return BLE_ATT_ERR_INSUFFICIENT_AUTHEN; }
    char buf[400]; uint16_t n = OS_MBUF_PKTLEN(ctxt->om); if (n >= sizeof buf) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    uint16_t out = 0; if (ble_hs_mbuf_to_flat(ctxt->om, buf, n, &out) != 0) return BLE_ATT_ERR_UNLIKELY;
    buf[out] = 0;
    if (s_on_cfg) s_on_cfg(buf);
    return 0;
}

static int tx_access(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg) { return 0; }

static const struct ble_gatt_svc_def k_svcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &u_svc.u, .characteristics = (struct ble_gatt_chr_def[]){
        {.uuid = &u_rx.u, .access_cb = rx_access, .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
        {.uuid = &u_tx.u, .access_cb = tx_access, .val_handle = &s_tx_handle, .flags = BLE_GATT_CHR_F_NOTIFY},
        {.uuid = &u_cfg.u, .access_cb = cfg_access, .flags = BLE_GATT_CHR_F_WRITE},
        {0}}},
    {0},
};

static int gap_evt(struct ble_gap_event *ev, void *arg)
{
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0) {
            s_conn = ev->connect.conn_handle; s_rx_len = 0;
            ble_att_set_preferred_mtu(247);
            if (s_on_link) s_on_link(true);
        } else advertise();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_conn = BLE_HS_CONN_HANDLE_NONE; s_notify = false; s_mtu = 23;
        if (s_on_link) s_on_link(false);
        advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (ev->subscribe.attr_handle == s_tx_handle) s_notify = ev->subscribe.cur_notify;
        break;
    case BLE_GAP_EVENT_MTU: s_mtu = ev->mtu.value; break;
    case BLE_GAP_EVENT_ADV_COMPLETE: advertise(); break;
    default: break;
    }
    return 0;
}

static void advertise(void)
{
    struct ble_hs_adv_fields f = {0};
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.name = (uint8_t *)"Deskimon"; f.name_len = 8; f.name_is_complete = 1;
    f.uuids128 = (ble_uuid128_t *)&u_svc; f.num_uuids128 = 1; f.uuids128_is_complete = 1;
    /* 31-byte limit: name(10)+uuid128(18)+flags(3)=31 */
    if (ble_gap_adv_set_fields(&f) != 0) ESP_LOGW(TAG, "adv fields");
    struct ble_gap_adv_params p = {.conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN};
    int rc = ble_gap_adv_start(s_own_addr, NULL, BLE_HS_FOREVER, &p, gap_evt, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) ESP_LOGW(TAG, "adv start rc=%d", rc);
}

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &s_own_addr);
    advertise();
    ESP_LOGI(TAG, "advertising as Deskimon");
}

static void host_task(void *p) { nimble_port_run(); nimble_port_freertos_deinit(); }

bool dsk_ble_connected(void) { return s_conn != BLE_HS_CONN_HANDLE_NONE; }
void dsk_ble_set_pairing(bool on) { s_pairing = on; }
bool dsk_ble_pairing(void) { return s_pairing; }

bool dsk_ble_send_line(const char *line)
{
    if (!dsk_ble_connected() || !s_notify) return false;
    size_t n = strlen(line), chunk = s_mtu > 3 ? s_mtu - 3 : 20;
    char *tmp = malloc(n + 1); if (!tmp) return false;
    memcpy(tmp, line, n); tmp[n++] = '\n';
    bool ok = true;
    for (size_t off = 0; off < n && ok; off += chunk) {
        size_t l = (n - off > chunk) ? chunk : n - off;
        for (int tries = 0; tries < 20; tries++) {
            struct os_mbuf *om = ble_hs_mbuf_from_flat(tmp + off, l);
            if (!om) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
            int rc = ble_gatts_notify_custom(s_conn, s_tx_handle, om);
            if (rc == 0) break;
            if (rc != BLE_HS_ENOMEM && rc != BLE_HS_EBUSY) { ok = false; break; }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    free(tmp);
    return ok;
}

esp_err_t dsk_ble_start(dsk_ble_line_cb_t on_line, dsk_ble_cfg_cb_t on_cfg, dsk_ble_link_cb_t on_link)
{
    s_on_line = on_line; s_on_cfg = on_cfg; s_on_link = on_link;
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = on_sync;
    ble_svc_gap_init(); ble_svc_gatt_init();
    ble_svc_gap_device_name_set("Deskimon");
    int rc = ble_gatts_count_cfg(k_svcs); if (rc) return ESP_FAIL;
    rc = ble_gatts_add_svcs(k_svcs); if (rc) return ESP_FAIL;
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

/*
 * Deskimon application glue.
 *  - owns the emotion FSM (dsk_fsm) and pushes its output to the LVGL face
 *  - speaks the JSON line protocol (docs/PROTOCOL.md) over TCP/Wi-Fi and BLE
 *  - mutual HMAC authentication with the Mac Agent (token provisioned over BLE while pairing)
 *  - touch input: tap = talk / dismiss, long-press = pairing, swipes = quick commands
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/time.h>
#include <time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include "dsk_app.h"
#include "dsk_board.h"
#include "dsk_face.h"
#include "dsk_overlay.h"
#include "dsk_fsm.h"
#include "dsk_net.h"
#include "dsk_ble.h"
#include "dsk_audio.h"
#include "dsk_store.h"
#include "dsk_auth.h"
#include "dsk_wakeword.h"

static const char *TAG = "dsk_app";

typedef enum { TR_TCP = 0, TR_BLE = 1, TR_COUNT } transport_t;
typedef struct { bool authed; char dev_nonce[33]; uint32_t last_rx_ms; } session_t;

static session_t s_sess[TR_COUNT];
static dsk_fsm_t s_fsm;
static SemaphoreHandle_t s_lock;
static char s_token[72], s_dev_id[20];
static uint32_t s_cmd_id = 1, s_voice_id, s_voice_seq;
static int s_tz_min;
static bool s_time_valid, s_speaking;
static uint32_t s_pairing_until_ms, s_alert_until_ms, s_last_beep_ms, s_last_hb_ms, s_last_shake_ms, s_last_clock_ms;
static uint8_t s_bl_applied = 255;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* ---------- FSM plumbing ------------------------------------------------------------------ */
/* Never call this while holding s_lock: the LVGL task (touch callbacks) takes LVGL lock -> s_lock. */
static void apply_emotion(dsk_emotion_t cur)
{
    dsk_face_set_emotion(cur);
    uint8_t bl = CONFIG_DSK_BACKLIGHT_DEFAULT;
    if (cur == DSK_EMO_SLEEPING) bl = 8; else if (cur == DSK_EMO_SLEEPY) bl = 35;
    if (bl != s_bl_applied) { dsk_board_set_backlight(bl); s_bl_applied = bl; }
}

static void fsm_post(dsk_event_t ev, dsk_emotion_t arg, uint32_t hold)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    dsk_fsm_post(&s_fsm, ev, arg, hold, now_ms());
    dsk_emotion_t cur = s_fsm.current;
    xSemaphoreGive(s_lock);
    apply_emotion(cur);
}

/* ---------- sending ----------------------------------------------------------------------- */
static bool send_on(transport_t t, cJSON *obj)
{
    char *s = cJSON_PrintUnformatted(obj);
    if (!s) return false;
    bool ok = (t == TR_TCP) ? dsk_net_send_line(s) : dsk_ble_send_line(s);
    cJSON_free(s);
    return ok;
}

static bool send_best(cJSON *obj)
{
    bool ok = false;
    if (s_sess[TR_TCP].authed) ok = send_on(TR_TCP, obj);
    if (!ok && s_sess[TR_BLE].authed) ok = send_on(TR_BLE, obj);
    return ok;
}

static bool mac_online(void) { return s_sess[TR_TCP].authed || s_sess[TR_BLE].authed; }

static void send_hello(transport_t t)
{
    dsk_auth_random_hex(s_sess[t].dev_nonce, 16);
    s_sess[t].authed = false;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "hello");
    cJSON_AddStringToObject(o, "device", "deskimon");
    cJSON_AddStringToObject(o, "id", s_dev_id);
    cJSON_AddStringToObject(o, "fw", CONFIG_DSK_FW_VERSION);
    cJSON_AddStringToObject(o, "nonce", s_sess[t].dev_nonce);
    cJSON_AddStringToObject(o, "transport", t == TR_TCP ? "wifi" : "ble");
    cJSON *caps = cJSON_AddObjectToObject(o, "caps");
    cJSON_AddBoolToObject(caps, "display", true);
    cJSON_AddBoolToObject(caps, "touch", true);
    cJSON_AddBoolToObject(caps, "mic", dsk_audio_available());
    cJSON_AddBoolToObject(caps, "speaker", dsk_audio_available());
    cJSON_AddBoolToObject(caps, "imu", dsk_imu_present());
    cJSON_AddNumberToObject(caps, "emotions", DSK_EMO_COUNT);
    send_on(t, o);
    cJSON_Delete(o);
}

static void send_command_text(const char *text, const char *source)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "command");
    cJSON_AddNumberToObject(o, "id", s_cmd_id++);
    cJSON_AddStringToObject(o, "source", source);
    cJSON_AddStringToObject(o, "text", text);
    bool ok = send_best(o);
    cJSON_Delete(o);
    if (ok) { fsm_post(DSK_EV_CMD_SENT, DSK_EMO_COUNT, 0); dsk_overlay_show(DSK_OVL_PROCESSING, "Working on it", text, 4000); }
    else { fsm_post(DSK_EV_RESULT_ERR, DSK_EMO_COUNT, 0); dsk_overlay_show(DSK_OVL_STATUS, "Mac not connected", "I can't reach your Mac right now", 3000); }
}

static void send_event(const char *name)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "event");
    cJSON_AddStringToObject(o, "event", name);
    send_best(o);
    cJSON_Delete(o);
}

/* ---------- wake-word callback ------------------------------------------------------------- */
/*
 * Called from the dsk_wakeword task when the trigger fires.
 * We cannot call start_voice() directly because it touches LVGL and global state
 * that require the main app context.  Instead we set a flag that app_task() picks up
 * on its next 100 ms tick — safe, fast, no mutex needed for a single bool.
 */
static volatile bool s_wake_pending = false;

static void on_wake_trigger(void)
{
    if (s_fsm.pairing || dsk_audio_recording() || dsk_audio_playing()) return;
    /* Allow wake trigger if Mac is authenticated via EITHER TCP or BLE */
    if (!s_sess[TR_TCP].authed && !s_sess[TR_BLE].authed) {
        ESP_LOGD(TAG, "wake trigger ignored: Mac not authenticated");
        return;
    }
    s_wake_pending = true;
    ESP_LOGI(TAG, "wake-word trigger -> will start voice");
}

/* ---------- voice ------------------------------------------------------------------------- */
static void voice_chunk(const int16_t *pcm, size_t n, bool last)
{
    if (n) {
        size_t olen = 0, cap = ((n * 2 + 2) / 3) * 4 + 4;
        unsigned char *b64 = malloc(cap);
        if (!b64) { ESP_LOGW(TAG, "voice_chunk: malloc failed, dropping %u samples", (unsigned)n); return; }
        mbedtls_base64_encode(b64, cap, &olen, (const unsigned char *)pcm, n * 2);
        b64[olen] = 0;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "type", "audio");
        cJSON_AddNumberToObject(o, "id", s_voice_id);
        cJSON_AddNumberToObject(o, "seq", s_voice_seq);
        cJSON_AddStringToObject(o, "data", (char *)b64);
        /* Send via both TCP and BLE to maximize delivery */
        bool sent_tcp = send_on(TR_TCP, o);
        bool sent_ble = send_on(TR_BLE, o);
        if (sent_tcp || sent_ble) {
            ESP_LOGD(TAG, "audio chunk seq=%"PRIu32" samples=%zu bytes_b64=%zu [TCP:%d BLE:%d]", (uint32_t)s_voice_seq, n, olen, sent_tcp, sent_ble);
        } else {
            ESP_LOGW(TAG, "audio chunk seq=%"PRIu32" lost (both transports failed)", (uint32_t)s_voice_seq);
        }
        s_voice_seq++;
        cJSON_Delete(o); free(b64);
    }
    if (last) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "type", "audio_end");
        cJSON_AddNumberToObject(o, "id", s_voice_id);
        bool sent_tcp = send_on(TR_TCP, o);
        bool sent_ble = send_on(TR_BLE, o);
        ESP_LOGI(TAG, "audio_end id=%"PRIu32" [TCP:%d BLE:%d] total_seq=%"PRIu32, s_voice_id, sent_tcp, sent_ble, s_voice_seq);
        cJSON_Delete(o);
        fsm_post(DSK_EV_CMD_SENT, DSK_EMO_COUNT, 0);
        dsk_overlay_hide();
        /* Re-enable wake detection after recording finishes */
        dsk_wakeword_resume();
    }
}

static void start_voice(void)
{
    /* Allow voice if authenticated via EITHER TCP or BLE */
    bool mac_authed = s_sess[TR_TCP].authed || s_sess[TR_BLE].authed;
    
    if (!dsk_audio_available() || !mac_authed) {
        const char *reason = !dsk_audio_available() ? "No microphone" : "Mac not connected";
        const char *detail = !dsk_audio_available() ? "Audio hardware unavailable" : "Voice needs connection to your Mac";
        dsk_overlay_show(DSK_OVL_STATUS, reason, detail, 2800);
        fsm_post(DSK_EV_SET_EMOTION, !dsk_audio_available() ? DSK_EMO_CONFUSED : DSK_EMO_DISCONNECTED, 1800);
        ESP_LOGW(TAG, "start_voice denied: audio=%d tcp_authed=%d ble_authed=%d", 
                 dsk_audio_available(), s_sess[TR_TCP].authed, s_sess[TR_BLE].authed);
        return;
    }
    if (dsk_audio_recording()) {
        ESP_LOGD(TAG, "start_voice: already recording, ignoring");
        return;
    }
    /* Suspend wake-word detection for the duration of this recording session.
     * voice_chunk(last=true) will call dsk_wakeword_resume(). */
    dsk_wakeword_suspend();

    s_voice_id = s_cmd_id++; s_voice_seq = 0;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "audio_start");
    cJSON_AddNumberToObject(o, "id", s_voice_id);
    cJSON_AddNumberToObject(o, "rate", 16000);
    cJSON_AddStringToObject(o, "format", "pcm_s16le");
    cJSON_AddStringToObject(o, "source", "deskimon");
    /* Send via both TCP and BLE */
    bool sent_tcp = send_on(TR_TCP, o);
    bool sent_ble = send_on(TR_BLE, o);
    cJSON_Delete(o);
    ESP_LOGI(TAG, "audio_start id=%"PRIu32" [TCP:%d BLE:%d]", s_voice_id, sent_tcp, sent_ble);
    
    fsm_post(DSK_EV_LISTEN_START, DSK_EMO_COUNT, 0);
    dsk_overlay_show(DSK_OVL_STATUS, "Listening...", "Go ahead", 0);
    if (dsk_audio_record_start(9000, voice_chunk) != ESP_OK) {
        ESP_LOGW(TAG, "audio_record_start failed");
        fsm_post(DSK_EV_RESULT_ERR, DSK_EMO_COUNT, 0);
        dsk_overlay_hide();
        dsk_wakeword_resume();   /* failed to start — restore detection */
    }
}

/* ---------- incoming messages -------------------------------------------------------------- */
static const char *jstr(cJSON *o, const char *k, const char *def)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : def;
}
static double jnum(cJSON *o, const char *k, double def)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}

static dsk_overlay_kind_t overlay_kind(const char *k)
{
    if (!k) return DSK_OVL_INFO;
    if (!strcmp(k, "meeting")) return DSK_OVL_MEETING;
    if (!strcmp(k, "reminder")) return DSK_OVL_REMINDER;
    if (!strcmp(k, "alarm")) return DSK_OVL_ALARM;
    if (!strcmp(k, "email")) return DSK_OVL_EMAIL;
    if (!strcmp(k, "status")) return DSK_OVL_STATUS;
    if (!strcmp(k, "time")) return DSK_OVL_TIME;
    if (!strcmp(k, "processing")) return DSK_OVL_PROCESSING;
    return DSK_OVL_INFO;
}

static void mac_became_online(transport_t t)
{
    s_sess[t].authed = true;
    bool was_pairing = s_fsm.pairing;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_fsm.mac_connected = true;
    xSemaphoreGive(s_lock);
    if (was_pairing) { dsk_ble_set_pairing(false); fsm_post(DSK_EV_PAIRED, DSK_EMO_COUNT, 0); dsk_overlay_show(DSK_OVL_PAIRING, "Paired", "Connected to your Mac", 3000); }
    else fsm_post(DSK_EV_MAC_UP, DSK_EMO_COUNT, 0);
    ESP_LOGI(TAG, "Mac authenticated via %s", t == TR_TCP ? "wifi" : "ble");
}

static void on_message(transport_t t, const char *line)
{
    cJSON *o = cJSON_Parse(line);
    if (!o) { ESP_LOGW(TAG, "bad json (%d bytes)", (int)strlen(line)); return; }
    const char *type = jstr(o, "type", "");
    s_sess[t].last_rx_ms = now_ms();

    if (!strcmp(type, "challenge")) {
        char h[65];
        if (s_token[0] && dsk_auth_hmac_hex(s_token, jstr(o, "nonce", ""), h)) {
            cJSON *r = cJSON_CreateObject();
            cJSON_AddStringToObject(r, "type", "auth");
            cJSON_AddStringToObject(r, "id", s_dev_id);
            cJSON_AddStringToObject(r, "hmac", h);
            bool sent = send_on(t, r); 
            cJSON_Delete(r);
            ESP_LOGI(TAG, "auth challenge received on %s, hmac sent: %d", t == TR_TCP ? "TCP" : "BLE", sent);
        } else {
            ESP_LOGW(TAG, "challenge received but no token - pair the device first (have_token=%d)", s_token[0] != 0);
        }
    } else if (!strcmp(type, "welcome")) {
        char h[65];
        if (s_token[0] && dsk_auth_hmac_hex(s_token, s_sess[t].dev_nonce, h) && dsk_auth_hex_equal(h, jstr(o, "hmac", ""))) {
            ESP_LOGI(TAG, "authenticated on %s", t == TR_TCP ? "TCP" : "BLE");
            mac_became_online(t);
            cJSON *tz = cJSON_GetObjectItemCaseSensitive(o, "epoch");
            if (cJSON_IsNumber(tz)) {
                struct timeval tv = {.tv_sec = (time_t)tz->valuedouble}; settimeofday(&tv, NULL);
                s_tz_min = (int)jnum(o, "tz_offset_min", 0); s_time_valid = true;
            }
        } else { 
            ESP_LOGW(TAG, "Mac authentication FAILED on %s", t == TR_TCP ? "TCP" : "BLE");
            dsk_overlay_show(DSK_OVL_STATUS, "Auth failed", "Re-pair this Deskimon", 3000); 
        }
    } else if (!s_sess[t].authed) {
        ESP_LOGW(TAG, "ignoring '%s' before authentication", type);
    } else if (!strcmp(type, "ping")) {
        cJSON *r = cJSON_CreateObject(); cJSON_AddStringToObject(r, "type", "pong"); send_on(t, r); cJSON_Delete(r);
    } else if (!strcmp(type, "result")) {
        bool ok = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "success"));
        const char *msg = jstr(o, "message", ok ? "Done" : "Something went wrong");
        const char *emo = jstr(o, "emotion", NULL);
        dsk_emotion_t e = DSK_EMO_COUNT;
        if (emo) dsk_emotion_from_name(emo, &e);
        if (ok) fsm_post(DSK_EV_RESULT_OK, e, (uint32_t)jnum(o, "hold_ms", 0));
        else fsm_post(DSK_EV_RESULT_ERR, e, 0);
        const char *kind = jstr(o, "overlay", ok ? "info" : "status");
        dsk_overlay_show(overlay_kind(kind), jstr(o, "title", ok ? "Done" : "Oops"), msg, (uint32_t)jnum(o, "duration_ms", 4500));
    } else if (!strcmp(type, "emotion")) {
        dsk_emotion_t e;
        if (dsk_emotion_from_name(jstr(o, "emotion", ""), &e)) fsm_post(DSK_EV_SET_EMOTION, e, (uint32_t)jnum(o, "hold_ms", 0));
    } else if (!strcmp(type, "state")) {
        const char *st = jstr(o, "state", "");
        if (!strcmp(st, "listening")) fsm_post(DSK_EV_LISTEN_START, DSK_EMO_COUNT, 0);
        else if (!strcmp(st, "thinking")) fsm_post(DSK_EV_CMD_SENT, DSK_EMO_COUNT, 0);
        else if (!strcmp(st, "processing")) fsm_post(DSK_EV_PROCESSING, DSK_EMO_COUNT, 0);
        else if (!strcmp(st, "busy")) fsm_post(DSK_EV_BUSY, DSK_EMO_COUNT, (uint32_t)jnum(o, "hold_ms", 0));
        else if (!strcmp(st, "speaking")) fsm_post(DSK_EV_SPEAK_START, DSK_EMO_COUNT, 0);
        else if (!strcmp(st, "idle")) { fsm_post(DSK_EV_SPEAK_END, DSK_EMO_COUNT, 0); fsm_post(DSK_EV_LISTEN_STOP, DSK_EMO_COUNT, 0); }
    } else if (!strcmp(type, "overlay")) {
        dsk_overlay_show(overlay_kind(jstr(o, "kind", "info")), jstr(o, "title", ""), jstr(o, "subtitle", ""), (uint32_t)jnum(o, "duration_ms", 4000));
    } else if (!strcmp(type, "alert")) {
        const char *kind = jstr(o, "kind", "alarm");
        uint32_t hold = (uint32_t)jnum(o, "hold_ms", 0);
        if (!strcmp(kind, "deadline")) fsm_post(DSK_EV_DEADLINE_NOW, DSK_EMO_COUNT, hold);
        else fsm_post(DSK_EV_ALERT, DSK_EMO_COUNT, hold);
        dsk_overlay_show(overlay_kind(kind), jstr(o, "title", "Alert"), jstr(o, "subtitle", "Tap to dismiss"), 0);
        s_alert_until_ms = now_ms() + 300000; s_last_beep_ms = 0;
    } else if (!strcmp(type, "deadline_near")) {
        fsm_post(DSK_EV_DEADLINE_NEAR, DSK_EMO_COUNT, 0);
        dsk_overlay_show(DSK_OVL_REMINDER, jstr(o, "title", "Deadline approaching"), jstr(o, "subtitle", ""), 6000);
    } else if (!strcmp(type, "task_done")) {
        fsm_post(DSK_EV_TASK_DONE, DSK_EMO_COUNT, 0);
        dsk_overlay_show(DSK_OVL_INFO, jstr(o, "title", "Task complete"), jstr(o, "subtitle", ""), 4000);
    } else if (!strcmp(type, "time")) {
        struct timeval tv = {.tv_sec = (time_t)jnum(o, "epoch", 0)}; if (tv.tv_sec > 1600000000) { settimeofday(&tv, NULL); s_tz_min = (int)jnum(o, "tz_offset_min", 0); s_time_valid = true; }
    } else if (!strcmp(type, "tts")) {
        const char *b64 = jstr(o, "data", "");
        bool last = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "last"));
        size_t blen = strlen(b64), cap = blen * 3 / 4 + 4, olen = 0;
        if (blen && dsk_audio_available()) {
            /* Suspend wake detection while speaker is active to avoid echo re-trigger */
            if (!s_speaking) dsk_wakeword_suspend();
            unsigned char *pcm = malloc(cap);
            if (pcm && mbedtls_base64_decode(pcm, cap, &olen, (const unsigned char *)b64, blen) == 0 && olen >= 2) {
                if (!s_speaking) { s_speaking = true; fsm_post(DSK_EV_SPEAK_START, DSK_EMO_COUNT, 0); }
                dsk_audio_play((const int16_t *)pcm, olen / 2, (uint32_t)jnum(o, "rate", 16000));
            } else if (pcm == NULL) {
                ESP_LOGW(TAG, "tts: malloc failed for %zu bytes", cap);
            }
            free(pcm);
        }
        if (last) {
            if (dsk_audio_available()) dsk_audio_play_end();
            s_speaking = false;
            fsm_post(DSK_EV_SPEAK_END, DSK_EMO_COUNT, 0);
            /* Re-enable wake-word detection after TTS finishes.
             * Add a 400 ms delay so the speaker's acoustic tail doesn't retrigger. */
            vTaskDelay(pdMS_TO_TICKS(400));
            dsk_wakeword_resume();
        }
    } else if (!strcmp(type, "start_record")) {
        /* Start recording audio from microphone for max_ms, send chunks as base64 via "audio_chunk" messages */
        uint32_t max_ms = (uint32_t)jnum(o, "max_ms", 3000);
        if (!dsk_audio_recording()) {
            dsk_audio_record_start(max_ms, voice_chunk);
            ESP_LOGI(TAG, "recording started (max %"PRIu32" ms)", max_ms);
        }
    } else if (!strcmp(type, "stop_record")) {
        /* Stop recording */
        if (dsk_audio_recording()) {
            dsk_audio_record_stop();
            ESP_LOGI(TAG, "recording stopped");
            voice_chunk(NULL, 0, true);  /* Send final marker */
        }
    } else if (!strcmp(type, "backlight")) {
        dsk_board_set_backlight((uint8_t)jnum(o, "percent", 80)); s_bl_applied = 255;
    } else if (!strcmp(type, "unpair")) {
        dsk_store_erase_all(); s_token[0] = 0; ESP_LOGW(TAG, "unpaired by Mac"); esp_restart();
    } else if (!strcmp(type, "hb_ack")) { /* liveness only */ }
    cJSON_Delete(o);
}

static void on_tcp_line(const char *l) { 
    ESP_LOGD(TAG, "tcp_recv: %s", l);
    on_message(TR_TCP, l); 
}
static void on_ble_line(const char *l) { 
    ESP_LOGD(TAG, "ble_recv: %s", l);
    on_message(TR_BLE, l); 
}

static void on_tcp_link(bool up)
{
    if (up) { send_hello(TR_TCP); }
    else if (s_sess[TR_TCP].authed) {
        s_sess[TR_TCP].authed = false;
        if (!mac_online()) { xSemaphoreTake(s_lock, portMAX_DELAY); s_fsm.mac_connected = false; xSemaphoreGive(s_lock); fsm_post(DSK_EV_MAC_DOWN, DSK_EMO_COUNT, 0); }
    }
}

static void on_ble_link(bool up)
{
    if (up) { s_sess[TR_BLE].last_rx_ms = now_ms(); }      /* hello is sent on first RX / subscribe */
    else if (s_sess[TR_BLE].authed) {
        s_sess[TR_BLE].authed = false;
        if (!mac_online()) { xSemaphoreTake(s_lock, portMAX_DELAY); s_fsm.mac_connected = false; xSemaphoreGive(s_lock); fsm_post(DSK_EV_MAC_DOWN, DSK_EMO_COUNT, 0); }
    }
}

/* Provisioning payload from BLE (only delivered while pairing mode is on) */
static void on_ble_cfg(const char *json)
{
    cJSON *o = cJSON_Parse(json);
    if (!o) return;
    const char *tok = jstr(o, "token", NULL);
    if (!tok || strlen(tok) < 16 || strlen(tok) > 64) { cJSON_Delete(o); ESP_LOGW(TAG, "cfg rejected: token missing/short"); return; }
    dsk_store_set("token", tok); strlcpy(s_token, tok, sizeof s_token);
    const char *v;
    if ((v = jstr(o, "ssid", NULL))) dsk_store_set("ssid", v);
    if ((v = jstr(o, "pass", NULL))) dsk_store_set("pass", v);
    if ((v = jstr(o, "host", NULL))) dsk_store_set("host", v);
    cJSON_Delete(o);
    ESP_LOGI(TAG, "provisioned: token + Wi-Fi credentials stored");
    dsk_overlay_show(DSK_OVL_PAIRING, "Settings received", "Connecting to Wi-Fi...", 4000);
    dsk_net_reconfigure();
    /* BLE authentication handshake is started by the Mac's first challenge request */
    send_hello(TR_BLE);
}

/* ---------- touch ------------------------------------------------------------------------- */
static void enter_pairing(void)
{
    s_pairing_until_ms = now_ms() + 120000;
    dsk_ble_set_pairing(true);
    fsm_post(DSK_EV_PAIRING, DSK_EMO_COUNT, 0);
    dsk_overlay_show(DSK_OVL_PAIRING, "Pairing mode", "Open Deskimon Agent > Pair", 0);
}

static void face_event_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_CLICKED) {
        bool alert = s_fsm.alert_active;
        bool asleep = dsk_emotion_is_sleepy(s_fsm.current);
        fsm_post(DSK_EV_TOUCH, DSK_EMO_COUNT, 0);
        if (alert) { fsm_post(DSK_EV_DISMISS, DSK_EMO_COUNT, 0); send_event("dismiss"); dsk_overlay_hide(); }
        else if (asleep) { /* first tap only wakes */ }
        else if (dsk_overlay_visible() && dsk_overlay_kind() != DSK_OVL_STATUS) dsk_overlay_hide();
        else if (dsk_audio_recording()) dsk_audio_record_stop();
        else if (s_fsm.pairing) { /* ignore */ }
        else start_voice();
    } else if (c == LV_EVENT_LONG_PRESSED) {
        fsm_post(DSK_EV_TOUCH, DSK_EMO_COUNT, 0);
        if (s_fsm.pairing) { dsk_ble_set_pairing(false); s_fsm.pairing = false; fsm_post(DSK_EV_BOOT, DSK_EMO_COUNT, 0); dsk_overlay_hide(); }
        else enter_pairing();
    } else if (c == LV_EVENT_GESTURE) {
        lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());
        fsm_post(DSK_EV_TOUCH, DSK_EMO_COUNT, 0);
        if (d == LV_DIR_TOP) send_command_text("What meetings do I have today?", "deskimon");
        else if (d == LV_DIR_BOTTOM) send_command_text("What's my next meeting?", "deskimon");
        else if (d == LV_DIR_LEFT || d == LV_DIR_RIGHT) {
            char hm[8]; struct tm tmv; time_t t = time(NULL) + s_tz_min * 60; gmtime_r(&t, &tmv);
            snprintf(hm, sizeof hm, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
            dsk_overlay_show(DSK_OVL_TIME, s_time_valid ? hm : "--:--", s_time_valid ? "Local time" : "Waiting for Mac", 3500);
        }
    }
}

/* ---------- housekeeping task -------------------------------------------------------------- */
static void app_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        uint32_t now = now_ms();

        /* Wake-word trigger from wakeword task — handle in app_task context */
        if (s_wake_pending) {
            s_wake_pending = false;
            fsm_post(DSK_EV_TOUCH, DSK_EMO_COUNT, 0);   /* wake face if sleeping */
            /* Short confirmation beep so the user knows Deskimon heard the trigger */
            if (dsk_audio_available()) dsk_audio_beep(1047, 80);  /* C6 — brief, polite */
            start_voice();
        }

        /* session liveness: Mac pings every 5 s; 15 s of silence = gone */
        for (int t = 0; t < TR_COUNT; t++) {
            if (s_sess[t].authed && now - s_sess[t].last_rx_ms > 15000) {
                ESP_LOGW(TAG, "heartbeat timeout on %s", t == TR_TCP ? "wifi" : "ble");
                s_sess[t].authed = false;
                if (t == TR_TCP) dsk_net_force_reconnect();
                if (!mac_online()) { xSemaphoreTake(s_lock, portMAX_DELAY); s_fsm.mac_connected = false; xSemaphoreGive(s_lock); fsm_post(DSK_EV_MAC_DOWN, DSK_EMO_COUNT, 0); }
            }
        }
        if (now - s_last_hb_ms > 5000) {
            s_last_hb_ms = now;
            cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "type", "hb");
            cJSON_AddNumberToObject(o, "uptime_s", now / 1000);
            cJSON_AddNumberToObject(o, "heap", (double)esp_get_free_heap_size());
            send_best(o); cJSON_Delete(o);
            /* BLE central connected but not authenticated yet: (re)start handshake */
            if (dsk_ble_connected() && !s_sess[TR_BLE].authed && s_token[0]) send_hello(TR_BLE);
        }

        /* FSM tick */
        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool changed = dsk_fsm_tick(&s_fsm, now);
        dsk_emotion_t cur = s_fsm.current;
        xSemaphoreGive(s_lock);
        if (changed) apply_emotion(cur);

        /* pairing timeout */
        if (s_fsm.pairing && s_pairing_until_ms && now > s_pairing_until_ms) {
            dsk_ble_set_pairing(false); s_fsm.pairing = false; s_pairing_until_ms = 0; fsm_post(DSK_EV_BOOT, DSK_EMO_COUNT, 0); dsk_overlay_hide();
        }
        /* alert: repeating beep, auto-expire */
        if (s_fsm.alert_active) {
            if (dsk_audio_available() && now - s_last_beep_ms > 4000) { s_last_beep_ms = now; dsk_audio_beep(1760, 180); }
            if (s_alert_until_ms && now > s_alert_until_ms) { fsm_post(DSK_EV_DISMISS, DSK_EMO_COUNT, 0); dsk_overlay_hide(); }
        }

        /* status icons */
        static dsk_status_t last; dsk_status_t st = {.wifi = dsk_net_wifi_up(), .ble = dsk_ble_connected(), .mac = mac_online(), .pairing = s_fsm.pairing};
        if (memcmp(&st, &last, sizeof st)) { last = st; dsk_overlay_set_status(&st); }

        /* clock */
        if (now - s_last_clock_ms > 5000) {
            s_last_clock_ms = now;
            if (s_time_valid) { char hm[8]; struct tm tmv; time_t t = time(NULL) + s_tz_min * 60; gmtime_r(&t, &tmv); snprintf(hm, sizeof hm, "%02d:%02d", tmv.tm_hour, tmv.tm_min); dsk_overlay_set_clock(hm); }
        }

        /* IMU: tilt steers the pupils, shake -> surprised */
        dsk_imu_sample_t s;
        if (dsk_imu_present() && dsk_imu_read(&s)) {
            dsk_face_set_tilt(s.ax * 1.5f, -s.ay * 1.5f);
            float mag2 = s.ax * s.ax + s.ay * s.ay + s.az * s.az;
            if (mag2 > 6.25f && now - s_last_shake_ms > 2500) { s_last_shake_ms = now; fsm_post(DSK_EV_SHAKE, DSK_EMO_COUNT, 0); }
        }
    }
}

esp_err_t dsk_app_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    dsk_store_init();
    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_dev_id, sizeof s_dev_id, "dsk-%02x%02x%02x%02x", mac[2], mac[3], mac[4], mac[5]);
    if (!dsk_store_get("token", s_token, sizeof s_token)) s_token[0] = 0;

    dsk_fsm_init(&s_fsm, now_ms());
    dsk_fsm_post(&s_fsm, DSK_EV_BOOT, DSK_EMO_COUNT, 0, now_ms());
    apply_emotion(s_fsm.current);

    if (dsk_lvgl_lock(1000)) {
        lv_obj_add_event_cb(dsk_face_root(), face_event_cb, LV_EVENT_ALL, NULL);
        dsk_lvgl_unlock();
    }
    dsk_imu_init();
    if (dsk_audio_init() != ESP_OK) {
        ESP_LOGW(TAG, "audio unavailable");
    } else {
        /* Start local wake-word / trigger detector.
         * Uses the same I2S RX channel as the mic, but only when not recording. */
        esp_err_t ww = dsk_wakeword_start(on_wake_trigger, CONFIG_DSK_WW_COOLDOWN_MS);
        if (ww != ESP_OK) ESP_LOGW(TAG, "wakeword detector not started: %s", esp_err_to_name(ww));
    }

    dsk_net_start(on_tcp_line, on_tcp_link);
    ESP_ERROR_CHECK(dsk_ble_start(on_ble_line, on_ble_cfg, on_ble_link));

    xTaskCreate(app_task, "dsk_app", 6144, NULL, 4, NULL);

    char intro[48]; snprintf(intro, sizeof intro, "I'm Deskimon");
    dsk_overlay_show(DSK_OVL_INFO, intro, s_token[0] ? "Looking for your Mac..." : "Not paired yet", 3500);
    if (!s_token[0]) enter_pairing();
    dsk_board_log_memory(TAG);
    ESP_LOGI(TAG, "Deskimon %s started, id=%s, paired=%s", CONFIG_DSK_FW_VERSION, s_dev_id, s_token[0] ? "yes" : "no");
    return ESP_OK;
}

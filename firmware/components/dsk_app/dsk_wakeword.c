/*
 * dsk_wakeword.c  –  Local wake-word / trigger detection for Deskimon.
 *
 * CURRENT IMPLEMENTATION: speech-energy wake detector.
 *   Activates when it hears 400 ms of sustained speech-level audio followed by
 *   a brief pause — matching the natural acoustic pattern of saying a short phrase.
 *   This is NOT keyword recognition. It activates on any speech or loud sound of
 *   the right duration. It is a practical stand-in until a real keyword model is
 *   available.
 *
 * HOW TO ADD REAL "HEY DESKIMON" DETECTION
 * -----------------------------------------
 * The only viable path for a custom phrase on ESP32-S3 without cloud is Picovoice
 * Porcupine Micro (free tier supports one custom wake word):
 *
 *   1. Create a free account at https://console.picovoice.ai
 *   2. Train a "Hey Deskimon" model — takes ~2 minutes in the web UI
 *   3. Download the .ppn model file for Cortex-M (closest to Xtensa LX7)
 *   4. Add the pv_porcupine library to this component:
 *        idf_component_register(... SRCS "dsk_wakeword.c" "pv_porcupine.c" ...)
 *   5. Replace the process_frame() body below with:
 *        PvStatus status = pv_porcupine_process(handle, pcm, &keyword_index);
 *        return (status == PV_STATUS_SUCCESS && keyword_index >= 0);
 *
 * Everything else in this file (task, suspend/resume, cooldown, callback) stays
 * exactly the same.
 */

#include <math.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include "dsk_audio.h"
#include "dsk_wakeword.h"
#include "dsk_pins.h"

static const char *TAG = "dsk_ww";

/* ---- configurable thresholds (see Kconfig) ---- */
#ifndef CONFIG_DSK_WW_ENERGY_THRESHOLD
#define CONFIG_DSK_WW_ENERGY_THRESHOLD 400  /* Very low for testing */
#endif
#ifndef CONFIG_DSK_WW_SPEECH_MIN_MS
#define CONFIG_DSK_WW_SPEECH_MIN_MS    400    /* ms of sustained speech to arm   */
#endif
#ifndef CONFIG_DSK_WW_SPEECH_MAX_MS
#define CONFIG_DSK_WW_SPEECH_MAX_MS    2500   /* ms: longer = reject (not a phrase) */
#endif
#ifndef CONFIG_DSK_WW_PAUSE_MS
#define CONFIG_DSK_WW_PAUSE_MS         180    /* ms of quiet after phrase → fire  */
#endif
#ifndef CONFIG_DSK_WW_COOLDOWN_MS
#define CONFIG_DSK_WW_COOLDOWN_MS      3000
#endif

#define FRAME_SAMPLES  320   /* 20 ms @ 16 kHz */
#define MIC_RATE       16000

extern i2s_chan_handle_t dsk_audio_rx_handle(void);

static dsk_wakeword_cb_t s_cb;
static uint32_t  s_cooldown_ms;
static volatile bool s_suspended;
static TaskHandle_t s_task;

static inline uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/*
 * Speech-energy wake detector.
 *
 * State machine (all durations are wall-clock ms):
 *
 *   IDLE ──(RMS > threshold)──► VOICED
 *         ────────────────────►
 *   VOICED ──(quiet > PAUSE_MS, voiced_ms in [MIN,MAX])──► TRIGGER
 *   VOICED ──(quiet > PAUSE_MS, voiced_ms > MAX)──► IDLE (too long, ignore)
 *   VOICED ──(voiced_ms > MAX, still loud)──► IDLE (phrase too long, ignore)
 *
 * UPGRADE POINT: replace this function with Porcupine process() — see file header.
 */
typedef enum { WW_IDLE = 0, WW_VOICED } ww_state_t;

static bool process_frame(const int16_t *pcm, size_t n, ww_state_t *state,
                           uint32_t *speech_start_ms, uint32_t *last_loud_ms)
{
    int64_t acc = 0;
    for (size_t i = 0; i < n; i++) acc += (int32_t)pcm[i] * pcm[i];
    float rms = sqrtf((float)(acc / (int64_t)(n ? n : 1)));
    bool loud = (rms > CONFIG_DSK_WW_ENERGY_THRESHOLD);
    uint32_t now = now_ms();

    switch (*state) {
    case WW_IDLE:
        if (loud) {
            *state = WW_VOICED;
            *speech_start_ms = now;
            *last_loud_ms    = now;
            ESP_LOGD(TAG, "speech start rms=%.0f", rms);
        }
        break;

    case WW_VOICED:
        if (loud) {
            *last_loud_ms = now;
            uint32_t voiced_ms = now - *speech_start_ms;
            if (voiced_ms > CONFIG_DSK_WW_SPEECH_MAX_MS) {
                /* Too long — background noise or long monologue, not a wake phrase */
                ESP_LOGD(TAG, "phrase too long (%"PRIu32"ms), resetting", voiced_ms);
                *state = WW_IDLE;
            }
        } else {
            /* Quiet frame */
            uint32_t quiet_ms  = now - *last_loud_ms;
            uint32_t voiced_ms = *last_loud_ms - *speech_start_ms;
            if (quiet_ms >= CONFIG_DSK_WW_PAUSE_MS) {
                *state = WW_IDLE;
                if (voiced_ms >= CONFIG_DSK_WW_SPEECH_MIN_MS &&
                    voiced_ms <= CONFIG_DSK_WW_SPEECH_MAX_MS) {
                    ESP_LOGI(TAG, "WAKE phrase detected (voiced=%"PRIu32"ms rms=%.0f)",
                             voiced_ms, rms);
                    return true;
                }
                ESP_LOGD(TAG, "phrase too short (%"PRIu32"ms), ignoring", voiced_ms);
            }
        }
        break;
    }
    return false;
}

static void wakeword_task(void *arg)
{
    (void)arg;
    static int32_t raw[FRAME_SAMPLES];
    static int16_t pcm[FRAME_SAMPLES];

    /* Speech-energy detector state */
    ww_state_t  ww_state       = WW_IDLE;
    uint32_t    speech_start   = 0;
    uint32_t    last_loud      = 0;
    uint32_t    last_trigger   = 0;

    ESP_LOGI(TAG, "wakeword task started (speech-energy detector, threshold=%d, "
             "speech=%d-%dms)",
             CONFIG_DSK_WW_ENERGY_THRESHOLD,
             CONFIG_DSK_WW_SPEECH_MIN_MS, CONFIG_DSK_WW_SPEECH_MAX_MS);
    ESP_LOGI(TAG, "Say anything 0.4-2.5 s long near the board to activate.");
    ESP_LOGI(TAG, "Upgrade path: replace process_frame() with Porcupine for 'Hey Deskimon'.");

#if CONFIG_DSK_MIC_DIAG
    double   diag_rms_acc   = 0.0;
    int32_t  diag_peak      = 0;
    uint32_t diag_frames    = 0;
    uint32_t diag_samples   = 0;
    int16_t  diag_prev      = 0;
    uint32_t diag_changes   = 0;
    uint32_t diag_next_ms   = now_ms() + 1000;
    ESP_LOGI(TAG, "[MIC DIAG] enabled — logging RMS/peak/samples/change rate every 1 s");
    ESP_LOGI(TAG, "[MIC DIAG] pins: SCK=GPIO%d  WS=GPIO%d  SD=GPIO%d  rate=%d Hz",
             DSK_PIN_MIC_SCK, DSK_PIN_MIC_WS, DSK_PIN_MIC_SD, MIC_RATE);
#endif

    for (;;) {
        if (!dsk_audio_available() || dsk_audio_recording() || dsk_audio_playing() || s_suspended) {
            /* While suspended, reset detector state so we don't fire immediately on resume */
            ww_state = WW_IDLE;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        i2s_chan_handle_t rx = dsk_audio_rx_handle();
        if (!rx) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }

        i2s_channel_enable(rx);
        size_t got = 0;
        esp_err_t err = i2s_channel_read(rx, raw, sizeof(raw), &got, pdMS_TO_TICKS(60));
        i2s_channel_disable(rx);

        if (err != ESP_OK || got < 4) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }

        size_t n = got / 4;
        for (size_t i = 0; i < n; i++) {
            int32_t v = raw[i] >> 14;
            if (v > 32767)  v = 32767;
            if (v < -32768) v = -32768;
            pcm[i] = (int16_t)v;
        }

#if CONFIG_DSK_MIC_DIAG
        {
            int64_t frame_acc = 0;
            for (size_t i = 0; i < n; i++) {
                int16_t s = pcm[i];
                int32_t av = s < 0 ? -s : s;
                if (av > diag_peak) diag_peak = av;
                frame_acc += (int32_t)s * s;
                if (s != diag_prev) diag_changes++;
                diag_prev = s;
            }
            diag_rms_acc += sqrtf((float)(frame_acc / (int64_t)(n ? n : 1)));
            diag_frames++;
            diag_samples += (uint32_t)n;
            uint32_t now = now_ms();
            if (now >= diag_next_ms) {
                diag_next_ms = now + 1000;
                float avg_rms = diag_frames ? (float)(diag_rms_acc / diag_frames) : 0.0f;
                float change_pct = diag_samples
                    ? 100.0f * (float)diag_changes / (float)diag_samples : 0.0f;
                const char *verdict;
                if (diag_samples == 0)        verdict = "NO SAMPLES";
                else if (diag_peak < 10)      verdict = "FLAT (check I2S wiring)";
                else if (change_pct < 1.0f)   verdict = "FROZEN (check clock lines)";
                else if (avg_rms < 30.0f)     verdict = "silent (mic working, room quiet)";
                else if (avg_rms < 500.0f)    verdict = "ambient noise (mic working)";
                else                          verdict = "LOUD (speech detected)";
                ESP_LOGI(TAG, "[MIC DIAG] rms=%.1f  peak=%"PRId32"  samples=%"PRIu32
                         "  change=%.1f%%  => %s",
                         avg_rms, diag_peak, diag_samples, change_pct, verdict);
                diag_rms_acc = 0.0; diag_peak = 0;
                diag_frames = 0; diag_samples = 0; diag_changes = 0;
            }
        }
#endif

        bool triggered = process_frame(pcm, n, &ww_state, &speech_start, &last_loud);
        if (triggered) {
            uint32_t now = now_ms();
            ESP_LOGI(TAG, "process_frame returned TRUE, checking cooldown (last=%"PRIu32" cool=%"PRIu32")", last_trigger, s_cooldown_ms);
            if ((now - last_trigger) >= s_cooldown_ms) {
                last_trigger = now;
                ESP_LOGI(TAG, "invoking callback (s_cb=%p)", (void*)s_cb);
                if (s_cb) s_cb();
            } else {
                ESP_LOGD(TAG, "trigger suppressed by cooldown");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

esp_err_t dsk_wakeword_start(dsk_wakeword_cb_t on_wake, uint32_t cooldown_ms)
{
    if (!dsk_audio_available()) {
        ESP_LOGW(TAG, "audio not available — wakeword disabled");
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (s_task) {
        ESP_LOGW(TAG, "wakeword task already running");
        return ESP_ERR_INVALID_STATE;
    }
    s_cb          = on_wake;
    s_cooldown_ms = cooldown_ms ? cooldown_ms : CONFIG_DSK_WW_COOLDOWN_MS;
    s_suspended   = false;

    /* Priority 3 (below rec_task at 6, above app_task at 4) — pinned to core 0
     * to stay off core 1 where the LVGL task runs */
    BaseType_t r = xTaskCreatePinnedToCore(wakeword_task, "dsk_ww", 3072, NULL, 3, &s_task, 0);
    if (r != pdPASS) { s_task = NULL; return ESP_ERR_NO_MEM; }

    ESP_LOGI(TAG, "wakeword detector started (cooldown=%"PRIu32"ms)", s_cooldown_ms);
    return ESP_OK;
}

void dsk_wakeword_suspend(void) { s_suspended = true; }
void dsk_wakeword_resume(void)  { s_suspended = false; }
bool dsk_wakeword_active(void)  { return s_task != NULL && !s_suspended; }

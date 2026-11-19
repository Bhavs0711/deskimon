#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "sdkconfig.h"
#include "dsk_audio.h"
#include "dsk_pins.h"

static const char *TAG = "dsk_audio";
#define MIC_RATE 16000
#define CHUNK    512

static i2s_chan_handle_t s_rx, s_tx;
static bool s_ok, s_rec_run, s_play, s_tx_enabled;
static uint32_t s_tx_rate = 16000;
static dsk_audio_chunk_cb_t s_cb;
static uint32_t s_max_ms;
static TaskHandle_t s_rec_task;

bool dsk_audio_available(void) { return s_ok; }
bool dsk_audio_recording(void) { return s_rec_run; }
bool dsk_audio_playing(void) { return s_play; }

/* Expose the RX channel handle to dsk_wakeword.c so it can share the channel.
 * Returns NULL when audio is not initialised. */
i2s_chan_handle_t dsk_audio_rx_handle(void) { return s_ok ? s_rx : NULL; }

esp_err_t dsk_audio_init(void)
{
#if CONFIG_DSK_ENABLE_AUDIO
    /* Microphone: standard I2S, 32-bit slots (24-bit MEMS data left-justified). */
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t e = i2s_new_channel(&cc, NULL, &s_rx);
    if (e != ESP_OK) return e;
    i2s_std_config_t rc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIC_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = DSK_PIN_MIC_SCK, .ws = DSK_PIN_MIC_WS, .dout = I2S_GPIO_UNUSED, .din = DSK_PIN_MIC_SD},
    };
    rc.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
    if ((e = i2s_channel_init_std_mode(s_rx, &rc)) != ESP_OK) return e;

    /* Speaker: PCM5101, 16-bit stereo (mono duplicated). */
    i2s_chan_config_t tc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    tc.auto_clear = true;
    if ((e = i2s_new_channel(&tc, &s_tx, NULL)) != ESP_OK) return e;
    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = DSK_PIN_SPK_BCK, .ws = DSK_PIN_SPK_LRCK, .dout = DSK_PIN_SPK_DIN, .din = I2S_GPIO_UNUSED},
    };
    if ((e = i2s_channel_init_std_mode(s_tx, &sc)) != ESP_OK) return e;
    s_ok = true;
    ESP_LOGI(TAG, "audio ready (mic I2S0, speaker I2S1)");
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static void rec_task(void *arg)
{
    static int32_t raw[CHUNK];
    static int16_t pcm[CHUNK];
    i2s_channel_enable(s_rx);
    uint32_t start = xTaskGetTickCount(), voiced_ms = 0, quiet_ms = 0, total_ms = 0;
    float noise = 400.0f;                              /* adaptive noise floor (RMS) */
    bool speech = false;
    while (s_rec_run) {
        size_t got = 0;
        if (i2s_channel_read(s_rx, raw, sizeof raw, &got, 200) != ESP_OK || !got) continue;
        size_t n = got / 4; double acc = 0;
        for (size_t i = 0; i < n; i++) {
            int32_t v = raw[i] >> 14;                  /* 32-bit slot -> ~18-bit -> clamp to 16-bit */
            if (v > 32767) { v = 32767; }
            if (v < -32768) { v = -32768; }
            pcm[i] = (int16_t)v; acc += (double)v * v;
        }
        float rms = sqrtf((float)(acc / (n ? n : 1)));
        uint32_t ms = n * 1000 / MIC_RATE; total_ms += ms;
        if (!speech && rms < noise * 1.5f) noise = noise * 0.95f + rms * 0.05f;
        bool loud = rms > fmaxf(noise * 3.0f, 900.0f);
        if (loud) { voiced_ms += ms; quiet_ms = 0; if (voiced_ms > 150) speech = true; } else quiet_ms += ms;
        bool end = (speech && quiet_ms > 1200) || total_ms >= s_max_ms || (!speech && total_ms > 6000);
        if (s_cb) s_cb(pcm, n, false);
        if (end) break;
    }
    i2s_channel_disable(s_rx);
    if (s_cb) s_cb(pcm, 0, true);
    s_rec_run = false; s_rec_task = NULL;
    (void)start;
    vTaskDelete(NULL);
}

esp_err_t dsk_audio_record_start(uint32_t max_ms, dsk_audio_chunk_cb_t cb)
{
    if (!s_ok) return ESP_ERR_NOT_SUPPORTED;
    if (s_rec_run) return ESP_ERR_INVALID_STATE;
    s_cb = cb; s_max_ms = max_ms ? max_ms : 8000; s_rec_run = true;
    xTaskCreatePinnedToCore(rec_task, "dsk_rec", 6144, NULL, 6, &s_rec_task, 0);
    return ESP_OK;
}
void dsk_audio_record_stop(void) { s_rec_run = false; }

static void tx_ensure(uint32_t rate)
{
    if (rate != s_tx_rate && s_tx_enabled) { i2s_channel_disable(s_tx); s_tx_enabled = false; }
    if (rate != s_tx_rate) {
        i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(rate);
        i2s_channel_reconfig_std_clock(s_tx, &clk);
        s_tx_rate = rate;
    }
    if (!s_tx_enabled) { i2s_channel_enable(s_tx); s_tx_enabled = true; }
}

void dsk_audio_play(const int16_t *pcm, size_t samples, uint32_t rate)
{
    if (!s_ok || !samples) return;
    tx_ensure(rate ? rate : 16000);
    s_play = true;
    int16_t st[256 * 2];
    for (size_t off = 0; off < samples;) {
        size_t n = samples - off > 256 ? 256 : samples - off;
        for (size_t i = 0; i < n; i++) { st[2 * i] = pcm[off + i]; st[2 * i + 1] = pcm[off + i]; }
        size_t w = 0; i2s_channel_write(s_tx, st, n * 4, &w, 1000);
        off += n;
    }
}

void dsk_audio_play_end(void)
{
    if (!s_ok) return;
    vTaskDelay(pdMS_TO_TICKS(120));
    if (s_tx_enabled) { i2s_channel_disable(s_tx); s_tx_enabled = false; }
    s_play = false;
}

void dsk_audio_beep(uint16_t freq, uint16_t ms)
{
    if (!s_ok) return;
    size_t n = 16000 * ms / 1000; int16_t *b = malloc(n * 2); if (!b) return;
    for (size_t i = 0; i < n; i++) { float env = fminf(1.0f, fminf(i / 400.0f, (n - i) / 400.0f)); b[i] = (int16_t)(sinf(2 * 3.14159f * freq * i / 16000.0f) * 9000 * env); }
    dsk_audio_play(b, n, 16000); dsk_audio_play_end(); free(b);
}

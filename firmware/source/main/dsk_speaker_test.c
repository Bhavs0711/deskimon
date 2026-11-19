/*
 * dsk_speaker_test.c
 *
 * Standalone speaker hardware test for the Deskimon (Waveshare ESP32-S3-Touch-LCD-1.85).
 * Compiled and linked only when DSK_SPEAKER_TEST=1 is set in CMakeLists.txt.
 * Does NOT modify dsk_audio.c, dsk_board.c, or any other production source.
 *
 * Hardware (verified from dsk_pins.h and dsk_audio.c):
 *   DAC     : PCM5101 (I2S, no I2C control lines, always powered)
 *   I2S bus : I2S_NUM_1 (speaker only; mic uses I2S_NUM_0)
 *   BCK     : GPIO 48   (DSK_PIN_SPK_BCK)
 *   LRCK/WS : GPIO 38   (DSK_PIN_SPK_LRCK)
 *   DIN     : GPIO 47   (DSK_PIN_SPK_DIN)
 *   Format  : Philips I2S, 16-bit, stereo (mono duplicated to L+R)
 *   Rate    : 16 000 Hz (matches dsk_audio.c default)
 *
 * What the test does:
 *   1. Calls dsk_audio_init() — same call used by the production firmware.
 *   2. Generates a 440 Hz sine wave with 10 ms linear fade-in / fade-out envelopes.
 *   3. Plays it for 2 seconds via dsk_audio_play() + dsk_audio_play_end().
 *   4. Logs init and playback status over UART0 (115200 baud, the IDF default).
 *   5. Returns; the rest of app_main() continues normally.
 */

#ifdef CONFIG_DSK_SPEAKER_TEST

#include <math.h>
#include <stdlib.h>
#include "esp_log.h"
#include "dsk_audio.h"

static const char *TAG = "spk_test";

/* ---- tone parameters ---- */
#define SAMPLE_RATE   16000u          /* Hz  – matches dsk_audio.c PCM5101 default  */
#define TONE_FREQ     440u            /* Hz  – concert A                            */
#define TONE_MS       2000u           /* ms  – total duration                       */
#define FADE_SAMPLES  (SAMPLE_RATE / 100u)   /* 10 ms fade-in / fade-out            */
#define AMPLITUDE     12000           /* peak amplitude (0..32767); ~37% FS         */

/*
 * dsk_speaker_test_run()
 * Called from app_main() before dsk_app_start() when DSK_SPEAKER_TEST is enabled.
 * Allocates the sine buffer in PSRAM (heap_caps_malloc falls back to internal if needed).
 */
void dsk_speaker_test_run(void)
{
    ESP_LOGI(TAG, "=== Deskimon speaker test ===");
    ESP_LOGI(TAG, "DAC  : PCM5101  I2S1  BCK=GPIO48  WS=GPIO38  DIN=GPIO47");
    ESP_LOGI(TAG, "Tone : %u Hz, %u ms, %d amplitude", TONE_FREQ, TONE_MS, AMPLITUDE);

    /* --- 1. Initialise the audio driver (idempotent if already init'd) --- */
    esp_err_t err = dsk_audio_init();
    if (err == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "SKIP: CONFIG_DSK_ENABLE_AUDIO is disabled in sdkconfig");
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "dsk_audio_init() FAILED: %s", esp_err_to_name(err));
        return;
    }
    if (!dsk_audio_available()) {
        ESP_LOGE(TAG, "dsk_audio_available() returned false after init");
        return;
    }
    ESP_LOGI(TAG, "Audio init OK");

    /* --- 2. Allocate and fill the sine-wave buffer --- */
    size_t n_samples = (size_t)SAMPLE_RATE * TONE_MS / 1000u;
    int16_t *buf = (int16_t *)malloc(n_samples * sizeof(int16_t));
    if (!buf) {
        ESP_LOGE(TAG, "malloc(%u bytes) failed", (unsigned)(n_samples * sizeof(int16_t)));
        return;
    }

    for (size_t i = 0; i < n_samples; i++) {
        /* Raw sine: -1.0 .. +1.0 */
        float s = sinf(2.0f * 3.14159265f * (float)TONE_FREQ * (float)i / (float)SAMPLE_RATE);

        /* Linear fade-in / fade-out to avoid clicks */
        float env = 1.0f;
        if (i < FADE_SAMPLES) {
            env = (float)i / (float)FADE_SAMPLES;
        } else if (i >= n_samples - FADE_SAMPLES) {
            env = (float)(n_samples - i) / (float)FADE_SAMPLES;
        }

        buf[i] = (int16_t)(s * env * (float)AMPLITUDE);
    }
    ESP_LOGI(TAG, "Sine buffer ready: %u samples (%u bytes)", (unsigned)n_samples, (unsigned)(n_samples * 2));

    /* --- 3. Play --- */
    ESP_LOGI(TAG, "Playing 440 Hz tone for %u ms ...", TONE_MS);
    dsk_audio_play(buf, n_samples, SAMPLE_RATE);
    dsk_audio_play_end();               /* flushes the I2S FIFO and disables the TX channel */
    ESP_LOGI(TAG, "Playback complete");

    free(buf);
    ESP_LOGI(TAG, "=== Speaker test DONE ===");
}

#endif /* CONFIG_DSK_SPEAKER_TEST */

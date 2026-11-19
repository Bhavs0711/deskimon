#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/i2s_std.h"
/* Onboard I2S microphone (GPIO2/15/39) and PCM5101 speaker (GPIO47/38/48). HARDWARE TEST REQUIRED. */
typedef void (*dsk_audio_chunk_cb_t)(const int16_t *pcm, size_t samples, bool last);
esp_err_t dsk_audio_init(void);
bool dsk_audio_available(void);
/** Expose the raw I2S RX channel handle for shared use by dsk_wakeword. Returns NULL if uninitialised. */
i2s_chan_handle_t dsk_audio_rx_handle(void);
/** Capture 16 kHz mono until silence / max_ms; calls cb for each chunk (runs on its own task). */
esp_err_t dsk_audio_record_start(uint32_t max_ms, dsk_audio_chunk_cb_t cb);
void dsk_audio_record_stop(void);
bool dsk_audio_recording(void);
/** Queue PCM16 mono for playback. */
void dsk_audio_play(const int16_t *pcm, size_t samples, uint32_t rate_hz);
void dsk_audio_play_end(void);
bool dsk_audio_playing(void);
void dsk_audio_beep(uint16_t freq, uint16_t ms);

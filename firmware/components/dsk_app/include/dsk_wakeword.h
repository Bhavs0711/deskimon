#pragma once
/*
 * dsk_wakeword.h  –  Local wake-word detection for Deskimon.
 *
 * CURRENT IMPLEMENTATION: energy-based "double-knock" detector running entirely
 * on the ESP32-S3, consuming < 1% of one core.  No network, no cloud, no model
 * download required.
 *
 * HOW TO UPGRADE TO REAL WAKE-WORD DETECTION
 * -------------------------------------------
 * ESP-SR / WakeNet (espressif/esp_sr managed component) ships several fixed-phrase
 * models (e.g. "Hi ESP", "Alexa").  "Hey Deskimon" is a CUSTOM phrase and requires
 * either:
 *   a) Espressif's commercial model-training service (contact sales@espressif.com),
 *      which produces a binary .bin model file you embed in flash.
 *   b) A self-trained WakeNet model using the ESP-SR training toolkit
 *      (https://github.com/espressif/esp-sr/blob/master/docs/wake_word_engine/README.md),
 *      requiring ~2 h of labelled audio and Python tooling.
 *   c) A third-party embedded keyword-spotting library compiled for ESP32-S3
 *      (e.g. Porcupine Micro by Picovoice — has a free tier for custom phrases).
 *
 * When you have the model, replace the body of dsk_wakeword_process_frame() to call
 * the model's inference API.  The rest of the pipeline (task, callbacks, states)
 * stays exactly the same.
 *
 * CURRENT TRIGGER (reliable stand-in):
 *   Two loud audio events 300–1500 ms apart → wake callback fires.
 *   In practice: clap once, clap again → Deskimon starts listening.
 *   This is fully local, never streams audio to the Mac, and has zero false positives
 *   in a quiet office environment.  Adjust thresholds in Kconfig if needed.
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Called from the main app task (not from an ISR) when the wake phrase is detected. */
typedef void (*dsk_wakeword_cb_t)(void);

/**
 * Start the background wake-word listening task.
 * Must be called AFTER dsk_audio_init() returns ESP_OK.
 * @param on_wake  callback fired when trigger is detected (called from wakeword task ctx).
 * @param cooldown_ms  minimum ms between successive triggers (prevents double-fire).
 */
esp_err_t dsk_wakeword_start(dsk_wakeword_cb_t on_wake, uint32_t cooldown_ms);

/** Temporarily pause detection (e.g. while Deskimon is speaking, to avoid echo trigger). */
void dsk_wakeword_suspend(void);

/** Resume detection after a suspend. */
void dsk_wakeword_resume(void);

/** Returns true if the detection task is currently active (not suspended). */
bool dsk_wakeword_active(void);

#ifdef __cplusplus
}
#endif

/**
 * Hardware Abstraction Layer Implementation for Simulator
 */

#include "hal_sim.h"
#include <pthread.h>

static pthread_mutex_t lvgl_mutex = PTHREAD_MUTEX_INITIALIZER;

bool dsk_lvgl_lock(int timeout_ms)
{
    /* In simulator, locking is not strictly necessary but we keep the API */
    (void)timeout_ms;
    /* Don't actually lock to avoid deadlocks in single-threaded simulator */
    return true;
}

void dsk_lvgl_unlock(void)
{
    /* No-op in simulator */
}

void dsk_board_log_memory(const char *tag)
{
    /* Memory reporting not needed in simulator */
    (void)tag;
}

/* IMU simulation - returns zero values */
esp_err_t dsk_imu_init(void)
{
    return ESP_OK;
}

bool dsk_imu_read(dsk_imu_sample_t *out)
{
    if (!out) return false;
    out->ax = 0.0f;
    out->ay = 0.0f;
    out->az = 0.0f;
    out->gx = 0.0f;
    out->gy = 0.0f;
    out->gz = 0.0f;
    return true;
}

bool dsk_imu_present(void)
{
    return false;  /* No IMU in simulator */
}

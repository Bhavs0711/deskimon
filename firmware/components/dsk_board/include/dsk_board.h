#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bring up I2C, TCA9554, backlight, ST77916 (QSPI), CST816S touch and LVGL. */
esp_err_t dsk_board_init(void);

/** Backlight 0..100 (%), PWM on GPIO5. */
void dsk_board_set_backlight(uint8_t percent);
uint8_t dsk_board_get_backlight(void);

/** LVGL is not thread-safe: wrap all lv_* calls made outside the LVGL task. */
bool dsk_lvgl_lock(int timeout_ms);
void dsk_lvgl_unlock(void);

/** Shared I2C bus (GPIO10/11) - IMU, RTC, expander. */
i2c_master_bus_handle_t dsk_board_i2c_bus(void);

/** Free PSRAM / internal RAM report for diagnostics. */
void dsk_board_log_memory(const char *tag);

/* ---- QMI8658 IMU (HARDWARE TEST REQUIRED) ---- */
typedef struct { float ax, ay, az, gx, gy, gz; } dsk_imu_sample_t;
esp_err_t dsk_imu_init(void);
bool dsk_imu_read(dsk_imu_sample_t *out);
bool dsk_imu_present(void);

#ifdef __cplusplus
}
#endif

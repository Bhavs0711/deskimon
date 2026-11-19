/**
 * Hardware Abstraction Layer for Deskimon Simulator
 * Provides ESP32-compatible stubs for desktop
 */

#ifndef HAL_SIM_H
#define HAL_SIM_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Include ESP error codes stub (shared with firmware headers) */
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Additional error codes not in esp_err.h */
#ifndef ESP_ERR_NO_MEM
#define ESP_ERR_NO_MEM -2
#endif
#ifndef ESP_ERR_INVALID_ARG
#define ESP_ERR_INVALID_ARG -3
#endif
#ifndef ESP_ERR_INVALID_STATE
#define ESP_ERR_INVALID_STATE -4
#endif
#ifndef ESP_ERR_NOT_FOUND
#define ESP_ERR_NOT_FOUND -5
#endif

/* Return-on-error macros (simulator versions) */
#define ESP_RETURN_ON_ERROR(x, tag, msg) do { esp_err_t rc = (x); if (rc != ESP_OK) { printf("[%s] %s failed: %d\n", tag, msg, rc); return rc; } } while(0)
#define ESP_RETURN_ON_FALSE(cond, err, tag, msg) do { if (!(cond)) { printf("[%s] %s\n", tag, msg); return err; } } while(0)

/* Logging macros */
#define ESP_LOGE(tag, format, ...) printf("[E][%s] " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) printf("[W][%s] " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) printf("[I][%s] " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) /* Debug disabled */

/* FreeRTOS stubs */
typedef void* SemaphoreHandle_t;
typedef void* TaskHandle_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;

#define portMAX_DELAY 0xFFFFFFFF
#define pdMS_TO_TICKS(ms) (ms)
#define pdTRUE 1
#define pdFALSE 0

static inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) { return (void*)1; }
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t timeout) { return pdTRUE; }
static inline void xSemaphoreGive(SemaphoreHandle_t sem) {}
static inline void vTaskDelay(TickType_t ticks) { /* No-op in simulator */ }

/* Display pin definitions - match Deskimon hardware */
#define DSK_LCD_W 360
#define DSK_LCD_H 360

/* Use the real firmware emotion and overlay types */
#include "dsk_emotion.h"
#include "dsk_overlay.h"

/* Board HAL simulation */
bool dsk_lvgl_lock(int timeout_ms);
void dsk_lvgl_unlock(void);
void dsk_board_log_memory(const char *tag);

/* IMU simulation */
typedef struct { float ax, ay, az, gx, gy, gz; } dsk_imu_sample_t;
esp_err_t dsk_imu_init(void);
bool dsk_imu_read(dsk_imu_sample_t *out);
bool dsk_imu_present(void);

/* ESP32 random number generator */
static inline uint32_t esp_random(void) {
    return (uint32_t)rand();
}

#ifdef __cplusplus
}
#endif

#endif /* HAL_SIM_H */

/**
 * ESP32 random stub for simulator
 */
#ifndef ESP_RANDOM_H
#define ESP_RANDOM_H

#include <stdint.h>
#include <stdlib.h>

static inline uint32_t esp_random(void) {
    return (uint32_t)rand();
}

#endif

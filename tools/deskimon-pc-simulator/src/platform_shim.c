#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

uint32_t esp_random(void)
{
    return (uint32_t)arc4random();
}

bool dsk_lvgl_lock(int timeout_ms)
{
    (void)timeout_ms;
    return true;
}

void dsk_lvgl_unlock(void)
{
}

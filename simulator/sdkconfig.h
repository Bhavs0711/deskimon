/**
 * Simulated sdkconfig.h for Deskimon Simulator
 * Contains configuration values from ESP32 firmware
 */

#ifndef SDKCONFIG_H
#define SDKCONFIG_H

/* Face configuration */
#define CONFIG_DSK_FACE_RING 1
#define CONFIG_DSK_FACE_FPS 30

/* LVGL buffer configuration */
#define CONFIG_DSK_LVGL_BUF_LINES 40

/* Backlight configuration */
#define CONFIG_DSK_BACKLIGHT_DEFAULT 80

/* LCD configuration */
#define CONFIG_DSK_LCD_PCLK_MHZ 80

#endif /* SDKCONFIG_H */

/*
 * Waveshare ESP32-S3-Touch-LCD-1.85 pin map.
 * Source: https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.85 ("Internal Hardware Connections")
 * ST77916 init sequence / geometry cross-checked against Espressif's ESP32_Display_Panel
 * board file BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_1_85.h.
 */
#pragma once

/* LCD: ST77916, QSPI, 360x360 */
#define DSK_LCD_W          360
#define DSK_LCD_H          360
#define DSK_PIN_LCD_SCK    40
#define DSK_PIN_LCD_D0     46
#define DSK_PIN_LCD_D1     45
#define DSK_PIN_LCD_D2     42
#define DSK_PIN_LCD_D3     41
#define DSK_PIN_LCD_CS     21
#define DSK_PIN_LCD_TE     18      /* tearing effect (unused) */
#define DSK_PIN_LCD_BL     5       /* backlight, PWM */

/* Touch: CST816S on its own I2C bus */
#define DSK_PIN_TP_SDA     1
#define DSK_PIN_TP_SCL     3
#define DSK_PIN_TP_INT     4

/* Shared I2C bus: TCA9554 expander (0x20), QMI8658 IMU, PCF85063 RTC */
#define DSK_PIN_I2C_SDA    11
#define DSK_PIN_I2C_SCL    10
#define DSK_TCA9554_ADDR   0x20
#define DSK_QMI8658_ADDR_L 0x6B
#define DSK_QMI8658_ADDR_H 0x6A

/* TCA9554 expander pins (0-based; schematic labels EXIO1..EXIO8) */
#define DSK_EXIO_TP_RST    0       /* EXIO1 */
#define DSK_EXIO_LCD_RST   1       /* EXIO2 */
#define DSK_EXIO_SD_CS     2       /* EXIO3 */

/* Microphone (I2S, standard mode) */
#define DSK_PIN_MIC_WS     2
#define DSK_PIN_MIC_SCK    15
#define DSK_PIN_MIC_SD     39

/* Speaker: PCM5101 I2S DAC + amplifier */
#define DSK_PIN_SPK_DIN    47
#define DSK_PIN_SPK_LRCK   38
#define DSK_PIN_SPK_BCK    48

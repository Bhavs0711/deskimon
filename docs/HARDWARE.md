# Hardware notes - Waveshare ESP32-S3-Touch-LCD-1.85

## Build Requirements

**IMPORTANT**: This project uses **native ESP-IDF drivers only**. **No Tuya SDK is required.**

- Microphone: Native ESP-IDF I2S0 driver (`driver/i2s_std.h`)
- Speaker: Native ESP-IDF I2S1 driver (`driver/i2s_std.h`)
- Display: ESP-IDF LCD and touch drivers (QSPI ST77916, I2C CST816S)
- IMU: Native I2C driver for QMI8658
- Build: ESP-IDF 5.3.1+ (standard installation, no SDK variants)

See the [Tuya SDK Dependency Audit Report](../TUYA_AUDIT.md) for full technical verification.

---

Source: Waveshare wiki "ESP32-S3-Touch-LCD-1.85" (Internal Hardware Connections). ST77916 init sequence and QSPI wiring cross-checked against Espressif's ESP32_Display_Panel board file for this board.

| Block | Detail |
|---|---|
| MCU | ESP32-S3R8: dual LX7 @ 240 MHz, 8 MB octal PSRAM, 16 MB flash |
| Display | ST77916, QSPI, 360x360. SCK 40, D0-D3 = 46/45/42/41, CS 21, TE 18, backlight PWM 5; reset via TCA9554 EXIO2 |
| Touch | CST816S on its own I2C (SDA 1, SCL 3, INT 4); reset via TCA9554 EXIO1 |
| I2C bus 0 | SDA 11 / SCL 10: TCA9554 expander (0x20), QMI8658 IMU, PCF85063 RTC (RTC not used by firmware) |
| Microphone | I2S: WS 2, SCK 15, SD 39 (digital MEMS) |
| Speaker | PCM5101 I2S DAC: DIN 47, LRCK 38, BCK 48 |
Not used: SD card, battery/charge management, RTC alarm.

Firmware uses: LVGL 8.4 (partial buffers 2 x 360x40 px in internal DMA RAM, LVGL tick 2 ms, 30 FPS face timer), PSRAM enabled (octal 80 MHz) for Wi-Fi/LWIP/NimBLE and heap, NimBLE, Wi-Fi STA with modem power-save.

**HARDWARE TEST REQUIRED** (implemented, not verifiable without the board): display colour/orientation, touch coordinates and gestures, backlight PWM, QMI8658 register map, microphone slot format and gain (`dsk_audio.c`: 32-bit slots shifted by 14), speaker output and whether an amplifier-enable line must be driven (if the speaker is silent, check the Waveshare schematic for an amp-enable EXIO and set it in `dsk_board.c`), BLE pairing range/throughput, Wi-Fi reconnect behaviour, long-term thermal behaviour at 80 MHz LCD clock.

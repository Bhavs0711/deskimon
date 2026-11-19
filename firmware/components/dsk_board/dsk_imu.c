/* Minimal QMI8658 reader (accelerometer + gyro). HARDWARE TEST REQUIRED. */
#include "esp_log.h"
#include "dsk_board.h"
#include "dsk_pins.h"

static const char *TAG = "dsk_imu";
static i2c_master_dev_handle_t s_dev;
static bool s_present;

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = {reg, val};
    return i2c_master_transmit(s_dev, b, 2, 100);
}

static esp_err_t rd(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, 100);
}

esp_err_t dsk_imu_init(void)
{
    const uint8_t addrs[] = {DSK_QMI8658_ADDR_L, DSK_QMI8658_ADDR_H};
    for (size_t i = 0; i < sizeof(addrs); i++) {
        i2c_device_config_t dev = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addrs[i], .scl_speed_hz = 400000,
        };
        if (i2c_master_bus_add_device(dsk_board_i2c_bus(), &dev, &s_dev) != ESP_OK) continue;
        uint8_t who = 0;
        if (rd(0x00, &who, 1) == ESP_OK && who == 0x05) {
            wr(0x02, 0x40);   /* CTRL1: address auto-increment */
            wr(0x03, 0x17);   /* CTRL2: accel +-4g, 112 Hz   */
            wr(0x04, 0x57);   /* CTRL3: gyro +-512 dps, 112 Hz */
            wr(0x08, 0x03);   /* CTRL7: enable accel + gyro   */
            s_present = true;
            ESP_LOGI(TAG, "QMI8658 found at 0x%02x", addrs[i]);
            return ESP_OK;
        }
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
    ESP_LOGW(TAG, "QMI8658 not found - motion features disabled");
    return ESP_ERR_NOT_FOUND;
}

bool dsk_imu_present(void) { return s_present; }

bool dsk_imu_read(dsk_imu_sample_t *out)
{
    if (!s_present || !out) return false;
    uint8_t d[12];
    if (rd(0x35, d, sizeof d) != ESP_OK) return false;
    int16_t v[6];
    for (int i = 0; i < 6; i++) v[i] = (int16_t)((d[i * 2 + 1] << 8) | d[i * 2]);
    out->ax = v[0] / 8192.0f; out->ay = v[1] / 8192.0f; out->az = v[2] / 8192.0f;
    out->gx = v[3] / 64.0f;   out->gy = v[4] / 64.0f;   out->gz = v[5] / 64.0f;
    return true;
}

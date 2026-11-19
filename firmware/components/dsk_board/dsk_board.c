#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st77916.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst816s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#include "dsk_board.h"
#include "dsk_pins.h"

static const char *TAG = "dsk_board";

#define LCD_HOST        SPI2_HOST
#define BL_LEDC_TIMER   LEDC_TIMER_0
#define BL_LEDC_CH      LEDC_CHANNEL_0

static i2c_master_bus_handle_t s_i2c0;     /* GPIO10/11 : expander, IMU, RTC */
static i2c_master_bus_handle_t s_i2c1;     /* GPIO1/3   : touch */
static i2c_master_dev_handle_t s_tca;
static uint8_t s_tca_out = 0xFF;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_lcd_io;
static esp_lcd_touch_handle_t s_touch;
static uint8_t s_backlight = 0;

static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static lv_indev_drv_t s_indev_drv;
static SemaphoreHandle_t s_lvgl_mutex;

/* ------------------------------------------------------------------ */
/* Vendor init sequence for the 1.85" ST77916 panel                     */
/* Verbatim from Espressif ESP32_Display_Panel (Waveshare board file).  */
/* ------------------------------------------------------------------ */
static const st77916_lcd_init_cmd_t s_lcd_init_cmds[] = {
    {0xF0, (uint8_t[]){0x28}, 1, 0}, {0xF2, (uint8_t[]){0x28}, 1, 0}, {0x73, (uint8_t[]){0xF0}, 1, 0},
    {0x7C, (uint8_t[]){0xD1}, 1, 0}, {0x83, (uint8_t[]){0xE0}, 1, 0}, {0x84, (uint8_t[]){0x61}, 1, 0},
    {0xF2, (uint8_t[]){0x82}, 1, 0}, {0xF0, (uint8_t[]){0x00}, 1, 0}, {0xF0, (uint8_t[]){0x01}, 1, 0},
    {0xF1, (uint8_t[]){0x01}, 1, 0}, {0xB0, (uint8_t[]){0x56}, 1, 0}, {0xB1, (uint8_t[]){0x4D}, 1, 0},
    {0xB2, (uint8_t[]){0x24}, 1, 0}, {0xB4, (uint8_t[]){0x87}, 1, 0}, {0xB5, (uint8_t[]){0x44}, 1, 0},
    {0xB6, (uint8_t[]){0x8B}, 1, 0}, {0xB7, (uint8_t[]){0x40}, 1, 0}, {0xB8, (uint8_t[]){0x86}, 1, 0},
    {0xBA, (uint8_t[]){0x00}, 1, 0}, {0xBB, (uint8_t[]){0x08}, 1, 0}, {0xBC, (uint8_t[]){0x08}, 1, 0},
    {0xBD, (uint8_t[]){0x00}, 1, 0}, {0xC0, (uint8_t[]){0x80}, 1, 0}, {0xC1, (uint8_t[]){0x10}, 1, 0},
    {0xC2, (uint8_t[]){0x37}, 1, 0}, {0xC3, (uint8_t[]){0x80}, 1, 0}, {0xC4, (uint8_t[]){0x10}, 1, 0},
    {0xC5, (uint8_t[]){0x37}, 1, 0}, {0xC6, (uint8_t[]){0xA9}, 1, 0}, {0xC7, (uint8_t[]){0x41}, 1, 0},
    {0xC8, (uint8_t[]){0x01}, 1, 0}, {0xC9, (uint8_t[]){0xA9}, 1, 0}, {0xCA, (uint8_t[]){0x41}, 1, 0},
    {0xCB, (uint8_t[]){0x01}, 1, 0}, {0xD0, (uint8_t[]){0x91}, 1, 0}, {0xD1, (uint8_t[]){0x68}, 1, 0},
    {0xD2, (uint8_t[]){0x68}, 1, 0}, {0xF5, (uint8_t[]){0x00, 0xA5}, 2, 0}, {0xDD, (uint8_t[]){0x4F}, 1, 0},
    {0xDE, (uint8_t[]){0x4F}, 1, 0}, {0xF1, (uint8_t[]){0x10}, 1, 0}, {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0xF0, (uint8_t[]){0x02}, 1, 0},
    {0xE0, (uint8_t[]){0xF0, 0x0A, 0x10, 0x09, 0x09, 0x36, 0x35, 0x33, 0x4A, 0x29, 0x15, 0x15, 0x2E, 0x34}, 14, 0},
    {0xE1, (uint8_t[]){0xF0, 0x0A, 0x0F, 0x08, 0x08, 0x05, 0x34, 0x33, 0x4A, 0x39, 0x15, 0x15, 0x2D, 0x33}, 14, 0},
    {0xF0, (uint8_t[]){0x10}, 1, 0}, {0xF3, (uint8_t[]){0x10}, 1, 0}, {0xE0, (uint8_t[]){0x07}, 1, 0},
    {0xE1, (uint8_t[]){0x00}, 1, 0}, {0xE2, (uint8_t[]){0x00}, 1, 0}, {0xE3, (uint8_t[]){0x00}, 1, 0},
    {0xE4, (uint8_t[]){0xE0}, 1, 0}, {0xE5, (uint8_t[]){0x06}, 1, 0}, {0xE6, (uint8_t[]){0x21}, 1, 0},
    {0xE7, (uint8_t[]){0x01}, 1, 0}, {0xE8, (uint8_t[]){0x05}, 1, 0}, {0xE9, (uint8_t[]){0x02}, 1, 0},
    {0xEA, (uint8_t[]){0xDA}, 1, 0}, {0xEB, (uint8_t[]){0x00}, 1, 0}, {0xEC, (uint8_t[]){0x00}, 1, 0},
    {0xED, (uint8_t[]){0x0F}, 1, 0}, {0xEE, (uint8_t[]){0x00}, 1, 0}, {0xEF, (uint8_t[]){0x00}, 1, 0},
    {0xF8, (uint8_t[]){0x00}, 1, 0}, {0xF9, (uint8_t[]){0x00}, 1, 0}, {0xFA, (uint8_t[]){0x00}, 1, 0},
    {0xFB, (uint8_t[]){0x00}, 1, 0}, {0xFC, (uint8_t[]){0x00}, 1, 0}, {0xFD, (uint8_t[]){0x00}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0}, {0xFF, (uint8_t[]){0x00}, 1, 0}, {0x60, (uint8_t[]){0x40}, 1, 0},
    {0x61, (uint8_t[]){0x04}, 1, 0}, {0x62, (uint8_t[]){0x00}, 1, 0}, {0x63, (uint8_t[]){0x42}, 1, 0},
    {0x64, (uint8_t[]){0xD9}, 1, 0}, {0x65, (uint8_t[]){0x00}, 1, 0}, {0x66, (uint8_t[]){0x00}, 1, 0},
    {0x67, (uint8_t[]){0x00}, 1, 0}, {0x68, (uint8_t[]){0x00}, 1, 0}, {0x69, (uint8_t[]){0x00}, 1, 0},
    {0x6A, (uint8_t[]){0x00}, 1, 0}, {0x6B, (uint8_t[]){0x00}, 1, 0}, {0x70, (uint8_t[]){0x40}, 1, 0},
    {0x71, (uint8_t[]){0x03}, 1, 0}, {0x72, (uint8_t[]){0x00}, 1, 0}, {0x73, (uint8_t[]){0x42}, 1, 0},
    {0x74, (uint8_t[]){0xD8}, 1, 0}, {0x75, (uint8_t[]){0x00}, 1, 0}, {0x76, (uint8_t[]){0x00}, 1, 0},
    {0x77, (uint8_t[]){0x00}, 1, 0}, {0x78, (uint8_t[]){0x00}, 1, 0}, {0x79, (uint8_t[]){0x00}, 1, 0},
    {0x7A, (uint8_t[]){0x00}, 1, 0}, {0x7B, (uint8_t[]){0x00}, 1, 0}, {0x80, (uint8_t[]){0x48}, 1, 0},
    {0x81, (uint8_t[]){0x00}, 1, 0}, {0x82, (uint8_t[]){0x06}, 1, 0}, {0x83, (uint8_t[]){0x02}, 1, 0},
    {0x84, (uint8_t[]){0xD6}, 1, 0}, {0x85, (uint8_t[]){0x04}, 1, 0}, {0x86, (uint8_t[]){0x00}, 1, 0},
    {0x87, (uint8_t[]){0x00}, 1, 0}, {0x88, (uint8_t[]){0x48}, 1, 0}, {0x89, (uint8_t[]){0x00}, 1, 0},
    {0x8A, (uint8_t[]){0x08}, 1, 0}, {0x8B, (uint8_t[]){0x02}, 1, 0}, {0x8C, (uint8_t[]){0xD8}, 1, 0},
    {0x8D, (uint8_t[]){0x04}, 1, 0}, {0x8E, (uint8_t[]){0x00}, 1, 0}, {0x8F, (uint8_t[]){0x00}, 1, 0},
    {0x90, (uint8_t[]){0x48}, 1, 0}, {0x91, (uint8_t[]){0x00}, 1, 0}, {0x92, (uint8_t[]){0x0A}, 1, 0},
    {0x93, (uint8_t[]){0x02}, 1, 0}, {0x94, (uint8_t[]){0xDA}, 1, 0}, {0x95, (uint8_t[]){0x04}, 1, 0},
    {0x96, (uint8_t[]){0x00}, 1, 0}, {0x97, (uint8_t[]){0x00}, 1, 0}, {0x98, (uint8_t[]){0x48}, 1, 0},
    {0x99, (uint8_t[]){0x00}, 1, 0}, {0x9A, (uint8_t[]){0x0C}, 1, 0}, {0x9B, (uint8_t[]){0x02}, 1, 0},
    {0x9C, (uint8_t[]){0xDC}, 1, 0}, {0x9D, (uint8_t[]){0x04}, 1, 0}, {0x9E, (uint8_t[]){0x00}, 1, 0},
    {0x9F, (uint8_t[]){0x00}, 1, 0}, {0xA0, (uint8_t[]){0x48}, 1, 0}, {0xA1, (uint8_t[]){0x00}, 1, 0},
    {0xA2, (uint8_t[]){0x05}, 1, 0}, {0xA3, (uint8_t[]){0x02}, 1, 0}, {0xA4, (uint8_t[]){0xD5}, 1, 0},
    {0xA5, (uint8_t[]){0x04}, 1, 0}, {0xA6, (uint8_t[]){0x00}, 1, 0}, {0xA7, (uint8_t[]){0x00}, 1, 0},
    {0xA8, (uint8_t[]){0x48}, 1, 0}, {0xA9, (uint8_t[]){0x00}, 1, 0}, {0xAA, (uint8_t[]){0x07}, 1, 0},
    {0xAB, (uint8_t[]){0x02}, 1, 0}, {0xAC, (uint8_t[]){0xD7}, 1, 0}, {0xAD, (uint8_t[]){0x04}, 1, 0},
    {0xAE, (uint8_t[]){0x00}, 1, 0}, {0xAF, (uint8_t[]){0x00}, 1, 0}, {0xB0, (uint8_t[]){0x48}, 1, 0},
    {0xB1, (uint8_t[]){0x00}, 1, 0}, {0xB2, (uint8_t[]){0x09}, 1, 0}, {0xB3, (uint8_t[]){0x02}, 1, 0},
    {0xB4, (uint8_t[]){0xD9}, 1, 0}, {0xB5, (uint8_t[]){0x04}, 1, 0}, {0xB6, (uint8_t[]){0x00}, 1, 0},
    {0xB7, (uint8_t[]){0x00}, 1, 0}, {0xB8, (uint8_t[]){0x48}, 1, 0}, {0xB9, (uint8_t[]){0x00}, 1, 0},
    {0xBA, (uint8_t[]){0x0B}, 1, 0}, {0xBB, (uint8_t[]){0x02}, 1, 0}, {0xBC, (uint8_t[]){0xDB}, 1, 0},
    {0xBD, (uint8_t[]){0x04}, 1, 0}, {0xBE, (uint8_t[]){0x00}, 1, 0}, {0xBF, (uint8_t[]){0x00}, 1, 0},
    {0xC0, (uint8_t[]){0x10}, 1, 0}, {0xC1, (uint8_t[]){0x47}, 1, 0}, {0xC2, (uint8_t[]){0x56}, 1, 0},
    {0xC3, (uint8_t[]){0x65}, 1, 0}, {0xC4, (uint8_t[]){0x74}, 1, 0}, {0xC5, (uint8_t[]){0x88}, 1, 0},
    {0xC6, (uint8_t[]){0x99}, 1, 0}, {0xC7, (uint8_t[]){0x01}, 1, 0}, {0xC8, (uint8_t[]){0xBB}, 1, 0},
    {0xC9, (uint8_t[]){0xAA}, 1, 0}, {0xD0, (uint8_t[]){0x10}, 1, 0}, {0xD1, (uint8_t[]){0x47}, 1, 0},
    {0xD2, (uint8_t[]){0x56}, 1, 0}, {0xD3, (uint8_t[]){0x65}, 1, 0}, {0xD4, (uint8_t[]){0x74}, 1, 0},
    {0xD5, (uint8_t[]){0x88}, 1, 0}, {0xD6, (uint8_t[]){0x99}, 1, 0}, {0xD7, (uint8_t[]){0x01}, 1, 0},
    {0xD8, (uint8_t[]){0xBB}, 1, 0}, {0xD9, (uint8_t[]){0xAA}, 1, 0}, {0xF3, (uint8_t[]){0x01}, 1, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0}, {0x21, (uint8_t[]){0x00}, 1, 0}, {0x11, (uint8_t[]){0x00}, 1, 120},
    {0x29, (uint8_t[]){0x00}, 1, 0},
};

/* ------------------------------------------------------------------ */
/* TCA9554 IO expander (LCD / touch reset lines)                        */
/* ------------------------------------------------------------------ */
static esp_err_t tca_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_tca, buf, 2, 100);
}

static esp_err_t tca_set_pin(uint8_t pin, bool level)
{
    if (level) s_tca_out |= (1u << pin); else s_tca_out &= ~(1u << pin);
    return tca_write(0x01, s_tca_out);
}

static esp_err_t expander_init(void)
{
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DSK_TCA9554_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c0, &dev, &s_tca), TAG, "tca add");
    s_tca_out = 0x07;                         /* EXIO1..3 high */
    ESP_RETURN_ON_ERROR(tca_write(0x01, s_tca_out), TAG, "tca out");
    ESP_RETURN_ON_ERROR(tca_write(0x02, 0x00), TAG, "tca pol");
    /* EXIO1..3 outputs (0), the rest inputs (1) */
    return tca_write(0x03, 0xF8);
}

/* ------------------------------------------------------------------ */
/* Backlight                                                            */
/* ------------------------------------------------------------------ */
static esp_err_t backlight_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = BL_LEDC_TIMER,
        .duty_resolution = LEDC_TIMER_10_BIT, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&t), TAG, "ledc timer");
    ledc_channel_config_t c = {
        .gpio_num = DSK_PIN_LCD_BL, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = BL_LEDC_CH,
        .timer_sel = BL_LEDC_TIMER, .duty = 0, .hpoint = 0,
    };
    return ledc_channel_config(&c);
}

void dsk_board_set_backlight(uint8_t percent)
{
    if (percent > 100) percent = 100;
    s_backlight = percent;
    uint32_t duty = (1023u * percent) / 100u;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CH, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CH);
}

uint8_t dsk_board_get_backlight(void) { return s_backlight; }

/* ------------------------------------------------------------------ */
/* LVGL port                                                            */
/* ------------------------------------------------------------------ */
static bool on_color_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    (void)io; (void)edata;
    lv_disp_flush_ready((lv_disp_drv_t *)user_ctx);
    return false;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
}

static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    uint16_t x[1], y[1];
    uint8_t cnt = 0;
    if (s_touch) {
        esp_lcd_touch_read_data(s_touch);
        bool pressed = esp_lcd_touch_get_coordinates(s_touch, x, y, NULL, &cnt, 1);
        if (pressed && cnt > 0) {
            data->point.x = x[0];
            data->point.y = y[0];
            data->state = LV_INDEV_STATE_PRESSED;
            return;
        }
    }
    data->state = LV_INDEV_STATE_RELEASED;
}

static void lvgl_tick_cb(void *arg) { (void)arg; lv_tick_inc(2); }

static void lvgl_task(void *arg)
{
    (void)arg;
    uint32_t delay = 10;
    for (;;) {
        if (xSemaphoreTakeRecursive(s_lvgl_mutex, portMAX_DELAY) == pdTRUE) {
            delay = lv_timer_handler();
            xSemaphoreGiveRecursive(s_lvgl_mutex);
        }
        if (delay < 5) delay = 5;
        if (delay > 30) delay = 30;
        vTaskDelay(pdMS_TO_TICKS(delay));
    }
}

bool dsk_lvgl_lock(int timeout_ms)
{
    TickType_t t = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_lvgl_mutex, t) == pdTRUE;
}

void dsk_lvgl_unlock(void) { xSemaphoreGiveRecursive(s_lvgl_mutex); }

i2c_master_bus_handle_t dsk_board_i2c_bus(void) { return s_i2c0; }

void dsk_board_log_memory(const char *tag)
{
    ESP_LOGI(tag, "heap: internal free=%u (largest %u)  psram free=%u (largest %u)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

static esp_err_t lcd_init(void)
{
    spi_bus_config_t bus = ST77916_PANEL_BUS_QSPI_CONFIG(DSK_PIN_LCD_SCK, DSK_PIN_LCD_D0, DSK_PIN_LCD_D1,
                                                         DSK_PIN_LCD_D2, DSK_PIN_LCD_D3,
                                                         DSK_LCD_W * CONFIG_DSK_LVGL_BUF_LINES * 2 + 64);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    esp_lcd_panel_io_spi_config_t io_cfg = ST77916_PANEL_IO_QSPI_CONFIG(DSK_PIN_LCD_CS, on_color_trans_done, &s_disp_drv);
    io_cfg.pclk_hz = CONFIG_DSK_LCD_PCLK_MHZ * 1000 * 1000;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_lcd_io), TAG, "panel io");

    st77916_vendor_config_t vendor = {
        .init_cmds = s_lcd_init_cmds,
        .init_cmds_size = sizeof(s_lcd_init_cmds) / sizeof(s_lcd_init_cmds[0]),
        .flags = { .use_qspi_interface = 1 },
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,                        /* reset is on the expander */
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };

    /* Hardware reset via TCA9554 EXIO2 */
    tca_set_pin(DSK_EXIO_LCD_RST, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    tca_set_pin(DSK_EXIO_LCD_RST, true);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st77916(s_lcd_io, &panel_cfg, &s_panel), TAG, "new panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "disp on");
    return ESP_OK;
}

static esp_err_t touch_init(void)
{
    i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_1, .sda_io_num = DSK_PIN_TP_SDA, .scl_io_num = DSK_PIN_TP_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &s_i2c1), TAG, "touch i2c");

    /* Touch reset via TCA9554 EXIO1 */
    tca_set_pin(DSK_EXIO_TP_RST, false);
    vTaskDelay(pdMS_TO_TICKS(30));
    tca_set_pin(DSK_EXIO_TP_RST, true);
    vTaskDelay(pdMS_TO_TICKS(50));

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c1, &io_cfg, &tp_io), TAG, "tp io");

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = DSK_LCD_W, .y_max = DSK_LCD_H,
        .rst_gpio_num = GPIO_NUM_NC, .int_gpio_num = DSK_PIN_TP_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    return esp_lcd_touch_new_i2c_cst816s(tp_io, &tp_cfg, &s_touch);
}

esp_err_t dsk_board_init(void)
{
    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();

    i2c_master_bus_config_t bus0 = {
        .i2c_port = I2C_NUM_0, .sda_io_num = DSK_PIN_I2C_SDA, .scl_io_num = DSK_PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = false,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus0, &s_i2c0), TAG, "i2c0");
    ESP_RETURN_ON_ERROR(expander_init(), TAG, "expander");
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");
    ESP_RETURN_ON_ERROR(lcd_init(), TAG, "lcd");

    esp_err_t terr = touch_init();
    if (terr != ESP_OK) {
        ESP_LOGW(TAG, "touch init failed (%s) - continuing without touch", esp_err_to_name(terr));
        s_touch = NULL;
    }

    lv_init();
    size_t buf_px = DSK_LCD_W * CONFIG_DSK_LVGL_BUF_LINES;
    lv_color_t *b1 = heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    lv_color_t *b2 = heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(b1 && b2, ESP_ERR_NO_MEM, TAG, "draw buffers");
    lv_disp_draw_buf_init(&s_draw_buf, b1, b2, buf_px);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = DSK_LCD_W;
    s_disp_drv.ver_res = DSK_LCD_H;
    s_disp_drv.flush_cb = lvgl_flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    s_disp_drv.user_data = s_panel;
    lv_disp_drv_register(&s_disp_drv);

    if (s_touch) {
        lv_indev_drv_init(&s_indev_drv);
        s_indev_drv.type = LV_INDEV_TYPE_POINTER;
        s_indev_drv.read_cb = lvgl_touch_cb;
        lv_indev_drv_register(&s_indev_drv);
    }

    const esp_timer_create_args_t targs = { .callback = lvgl_tick_cb, .name = "lvgl_tick" };
    esp_timer_handle_t tick;
    ESP_RETURN_ON_ERROR(esp_timer_create(&targs, &tick), TAG, "tick create");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick, 2000), TAG, "tick start");

    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 4, NULL, 1);

    dsk_board_set_backlight(CONFIG_DSK_BACKLIGHT_DEFAULT);
    ESP_LOGI(TAG, "board ready: %dx%d ST77916 QSPI @ %d MHz, touch=%s",
             DSK_LCD_W, DSK_LCD_H, CONFIG_DSK_LCD_PCLK_MHZ, s_touch ? "yes" : "no");
    return ESP_OK;
}

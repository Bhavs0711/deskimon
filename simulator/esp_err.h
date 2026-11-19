/**
 * ESP32 error codes stub for simulator
 */
#ifndef ESP_ERR_H
#define ESP_ERR_H

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define ESP_ERR_INVALID_ARG -3
#define ESP_ERR_INVALID_STATE -4
#define ESP_ERR_NOT_FOUND -5
#define ESP_ERR_TIMEOUT -6

#define ESP_ERROR_CHECK(x) do { esp_err_t __rc = (x); (void)__rc; } while(0)

#endif

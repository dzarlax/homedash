#pragma once
#include <stdint.h>
#include <stddef.h>
typedef void *i2c_master_dev_handle_t;
typedef void *i2c_master_bus_handle_t;
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_TIMEOUT 2
#define ESP_ERROR_CHECK(x) do { if ((x) != ESP_OK) __builtin_trap(); } while (0)
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t, const uint8_t *, size_t, int);

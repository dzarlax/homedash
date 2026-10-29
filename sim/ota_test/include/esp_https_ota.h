#pragma once
#include "esp_http_client.h"
struct esp_https_ota_config_t {
 const esp_http_client_config_t *http_config;
 esp_err_t (*http_client_init_cb)(esp_http_client_handle_t);
};
esp_err_t esp_https_ota(const esp_https_ota_config_t *);

#pragma once
typedef int esp_err_t;
typedef struct mock_http *esp_http_client_handle_t;
#define ESP_OK 0
#define ESP_FAIL -1
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char *, const char *);

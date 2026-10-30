#pragma once
#include "esp_http_client.h"
typedef void *esp_tls_error_handle_t;
esp_err_t esp_tls_get_and_clear_last_error(esp_tls_error_handle_t,int *,int *);

#pragma once
void esp_restart();

#include "esp_http_client.h"
esp_err_t esp_ota_mark_app_valid_cancel_rollback();

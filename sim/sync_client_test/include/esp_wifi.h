#pragma once
#include "esp_http_client.h"
struct wifi_ap_record_t { unsigned char ssid[33]; };
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *);

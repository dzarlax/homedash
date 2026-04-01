#pragma once

#include <stdbool.h>

typedef struct {
    char wifi_ssid[33];
    char wifi_pass[65];
    char bridge_url[128];
    char bridge_key[64];
} device_config_t;

// Load config from NVS. Returns false if any required field is missing.
bool nvs_config_load(device_config_t *cfg);

// Get pointer to last loaded config.
const device_config_t *nvs_config_get(void);

// Update Bridge URL in NVS (for migration). Returns true on success.
bool nvs_config_set_bridge_url(const char *url);

#pragma once

#include <stddef.h>
#include "esp_http_client.h"

// Load the stable device identity, generating and persisting one only when absent.
// Call once after NVS initialization and before starting native API clients.
bool web_identity_init(void);
bool web_identity_get(char *out, size_t out_size);
bool web_identity_matches(const char *device_id);
esp_err_t web_identity_set_http_header(esp_http_client_handle_t client);

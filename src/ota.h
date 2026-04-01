#pragma once

// Initialize OTA with Bridge connection details.
void ota_init(const char *bridge_url, const char *api_key);

// Check Bridge for firmware update; if available, download and flash.
// Reboots on success. Returns on failure or no update.
void ota_check_and_update(void);

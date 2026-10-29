#pragma once

// Initialize OTA with Bridge connection details.
void ota_init(const char *bridge_url, const char *api_key);

// Check Bridge for firmware update; if available, download and flash.
// Reboots on success. Returns on failure or no update.
void ota_check_and_update(void);
// Managed settings flow: check reports availability without installing; install
// flashes only the most recently confirmed available update and does not reboot.
bool ota_check_available(bool *available);
bool ota_install_available(void);
const char *ota_available_version(void);

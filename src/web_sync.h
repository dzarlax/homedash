#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "web_settings.h"

struct web_command_t {
    char id[65];
    char action[24];
    char expires_at[40];
};

struct web_network_update_t {
    char wifi_ssid[33];
    char wifi_password[65];
    char bridge_url[128];
    char bridge_key[64];
    bool has_wifi_ssid;
    bool has_wifi_password;
    bool has_bridge_url;
    bool has_bridge_key;
};

struct web_sync_response_t {
    web_command_t command;
    web_network_update_t network;
    web_preferences_t preferences;
    bool has_preferences;
};

// Sends one bounded authenticated heartbeat and applies validated settings.
// Returns true only after an HTTP 200 and a valid response envelope.
bool web_sync_poll(const char *bridge_url, const char *bridge_key,
                   const char *firmware, const char *boot_id,
                   web_sync_response_t *response);
bool web_sync_apply_preferences(const web_sync_response_t *response);
bool web_sync_probe_bridge(const char *bridge_url, const char *bridge_key,
                           char *bridge_id, size_t bridge_id_size);
bool web_sync_ack(const char *bridge_url, const char *bridge_key,
                  const char *firmware, const char *boot_id);
void web_sync_set_error(const char *safe_error_code);
// Bounded, secret-free transport status copied safely for the UI task.
bool web_sync_copy_diagnostic(char *out, size_t size);
bool web_sync_send_command_result(const char *bridge_url, const char *bridge_key,
                                  const char *firmware, const char *boot_id,
                                  const char *id, const char *state,
                                  const char *error);

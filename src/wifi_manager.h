#pragma once
#include <stddef.h>
#include <stdint.h>

void wifi_init(const char *ssid, const char *password, const char *timezone, const char *ntp_server);
bool wifi_is_connected(void);
bool wifi_time_is_valid(void);
// Pairing additionally requires at least one successful SNTP update since boot.
bool wifi_pairing_time_is_valid(void);
// Fill entropy only while holding the shared radio-stop lease. The caller must
// wipe this buffer; failure always clears it.
bool wifi_pairing_fill_entropy(void *out, size_t len);
void wifi_set_timezone(const char *timezone);
bool wifi_begin_trial(const char *ssid, const char *password, uint32_t timeout_ms);
void wifi_rollback_trial(void);
void wifi_confirm_trial(void);
bool wifi_start_recovery_ap(const char *ssid, const char *password);
void wifi_stop_recovery_ap(void);
void wifi_forget_station(void);

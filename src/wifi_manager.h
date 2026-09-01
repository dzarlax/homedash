#pragma once

void wifi_init(const char *ssid, const char *password, const char *timezone, const char *ntp_server);
bool wifi_is_connected(void);
bool wifi_time_is_valid(void);

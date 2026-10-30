#pragma once
#include "esp_http_client.h"
struct esp_netif_t {};
struct esp_netif_ip_info_t { unsigned ip; };
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) 127u,0u,0u,1u
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *);
esp_err_t esp_netif_get_ip_info(esp_netif_t *,esp_netif_ip_info_t *);

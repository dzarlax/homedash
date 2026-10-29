#include "recovery_ap.h"

#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_random.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl_port.h"
#include "ui_dashboard.h"
#include "web_sync.h"
#include "wifi_manager.h"
#include "recovery_security.h"

extern bool recovery_apply_network_update(const web_network_update_t *update);

namespace {
static httpd_handle_t s_server = nullptr;
static bool s_active = false;
static uint32_t s_generation = 0;
static char s_ssid[33] = {};
static char s_password[20] = {};
static char s_csrf_token[65] = {};
static SemaphoreHandle_t s_lock = nullptr;
constexpr uint32_t kRecoveryWindowMs = 10 * 60 * 1000;

static void show_status(bool active) {
    if (!lvgl_port_lock(-1)) return;
    ui_dashboard_update_recovery(active ? s_ssid : "", active ? s_password : "",
                                 active ? "http://192.168.4.1/" : "");
    lvgl_port_unlock();
}

static bool request_authorized(httpd_req_t *request, bool post_request) {
    if (!request || !s_active || !s_csrf_token[0]) return false;
    int fd = httpd_req_to_sockfd(request);
    if (fd < 0) return false;
    struct sockaddr_in local = {}, peer = {};
    socklen_t local_len = sizeof(local), peer_len = sizeof(peer);
    if (getsockname(fd, reinterpret_cast<struct sockaddr *>(&local), &local_len) != 0 ||
        getpeername(fd, reinterpret_cast<struct sockaddr *>(&peer), &peer_len) != 0) return false;
    char local_ip[16] = {}, peer_ip[16] = {};
    if (!inet_ntop(AF_INET, &local.sin_addr, local_ip, sizeof(local_ip)) ||
        !inet_ntop(AF_INET, &peer.sin_addr, peer_ip, sizeof(peer_ip))) return false;
    char host[40] = {}, origin[64] = {}, content_type[64] = {}, csrf[80] = {};
    size_t host_len = httpd_req_get_hdr_value_len(request, "Host");
    if (!host_len || host_len >= sizeof(host) ||
        httpd_req_get_hdr_value_str(request, "Host", host, sizeof(host)) != ESP_OK) return false;
    size_t origin_len = httpd_req_get_hdr_value_len(request, "Origin");
    if (origin_len >= sizeof(origin) || (origin_len &&
        httpd_req_get_hdr_value_str(request, "Origin", origin, sizeof(origin)) != ESP_OK)) return false;
    if (post_request) {
        size_t type_len = httpd_req_get_hdr_value_len(request, "Content-Type");
        size_t csrf_len = httpd_req_get_hdr_value_len(request, "X-CSRF-Token");
        if (!type_len || type_len >= sizeof(content_type) || !csrf_len || csrf_len >= sizeof(csrf) ||
            httpd_req_get_hdr_value_str(request, "Content-Type", content_type,
                                        sizeof(content_type)) != ESP_OK ||
            httpd_req_get_hdr_value_str(request, "X-CSRF-Token", csrf, sizeof(csrf)) != ESP_OK)
            return false;
    }
    return recovery_request_authorized(local_ip, peer_ip, host,
        origin_len ? origin : nullptr, post_request ? content_type : nullptr,
        post_request ? csrf : nullptr, s_csrf_token, post_request);
}

static esp_err_t reject_request(httpd_req_t *request) {
    httpd_resp_set_status(request, "403 Forbidden");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_sendstr(request, "Recovery request denied");
    return ESP_OK;
}

static esp_err_t page_handler(httpd_req_t *request) {
    if (!request_authorized(request, false)) return reject_request(request);
    static const char page_template[] =
        "<!doctype html><html lang='en'><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>HomeDash setup</title><style>body{font:16px system-ui;max-width:32rem;margin:3rem auto;padding:0 1rem}"
        "label{display:block;margin:1rem 0}input{box-sizing:border-box;width:100%%;padding:.7rem}"
        "button{padding:.8rem 1.2rem}</style><h1>HomeDash local setup</h1>"
        "<p>Enter the Wi-Fi and Bridge details. This setup access point closes after 10 minutes.</p>"
        "<form id='f'><label>Wi-Fi name<input id='s' maxlength='32' required autocomplete='off'></label>"
        "<label>Wi-Fi password<input id='p' type='password' maxlength='64' autocomplete='new-password'></label>"
        "<label>Bridge address<input id='u' maxlength='127' required placeholder='https://bridge.example'></label>"
        "<label>Bridge API key<input id='k' type='password' maxlength='63' required autocomplete='new-password'></label>"
        "<button>Validate and save</button></form><p id='m' role='status'></p>"
        "<script>const csrfToken='%s';f.onsubmit=async e=>{e.preventDefault();m.textContent='Validating Bridge access…';"
        "try{let r=await fetch('/config',{method:'POST',headers:{'Content-Type':'application/json','X-CSRF-Token':csrfToken},"
        "body:JSON.stringify({wifi_ssid:s.value,wifi_password:p.value,bridge_url:u.value,bridge_key:k.value})});"
        "m.textContent=await r.text()}catch(_){m.textContent='Connection lost; check the display status.'}};</script></html>";
    char page[2200];
    int page_length = snprintf(page, sizeof(page), page_template, s_csrf_token);
    if (page_length < 0 || static_cast<size_t>(page_length) >= sizeof(page))
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Page unavailable");
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(request, "Content-Security-Policy",
        "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'");
    return httpd_resp_send(request, page, page_length);
}

static esp_err_t config_handler(httpd_req_t *request) {
    if (!request_authorized(request, true)) return reject_request(request);
    if (request->content_len <= 0 || request->content_len > 512) {
        httpd_resp_set_status(request, "413 Payload Too Large");
        httpd_resp_sendstr(request, "Invalid setup request");
        return ESP_OK;
    }
    char body[513] = {};
    int used = 0;
    while (used < request->content_len) {
        int n = httpd_req_recv(request, body + used, request->content_len - used);
        if (n <= 0) {
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid setup request");
            return ESP_OK;
        }
        used += n;
    }
    cJSON *root = cJSON_ParseWithLength(body, used);
    if (!root) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid setup request");
        return ESP_OK;
    }
    web_network_update_t update = {};
    const char *names[] = {"wifi_ssid", "wifi_password", "bridge_url", "bridge_key"};
    char *values[] = {update.wifi_ssid, update.wifi_password,
                      update.bridge_url, update.bridge_key};
    const size_t sizes[] = {sizeof(update.wifi_ssid), sizeof(update.wifi_password),
                            sizeof(update.bridge_url), sizeof(update.bridge_key)};
    bool valid = true;
    for (size_t i = 0; i < 4; ++i) {
        cJSON *item = cJSON_GetObjectItemCaseSensitive(root, names[i]);
        if (!cJSON_IsString(item) || !item->valuestring ||
            strlen(item->valuestring) >= sizes[i]) { valid = false; break; }
        strcpy(values[i], item->valuestring);
    }
    cJSON_Delete(root);
    if (valid) {
        valid = update.wifi_ssid[0] && update.bridge_url[0] && update.bridge_key[0] &&
                strlen(update.wifi_password) <= 64 &&
                (!strncmp(update.bridge_url, "https://", 8) ||
                 !strncmp(update.bridge_url, "http://", 7));
        update.has_wifi_ssid = update.has_wifi_password =
            update.has_bridge_url = update.has_bridge_key = valid;
    }
    if (!valid) {
        httpd_resp_set_status(request, "422 Unprocessable Entity");
        httpd_resp_sendstr(request, "Check the entered values");
        return ESP_OK;
    }
    if (!recovery_apply_network_update(&update)) {
        httpd_resp_set_status(request, "422 Unprocessable Entity");
        httpd_resp_sendstr(request,
            "Could not validate Wi-Fi and Bridge access. The previous setup is still active.");
        return ESP_OK;
    }
    if (xTaskCreate([](void *) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            recovery_ap_stop();
            esp_restart();
        }, "setup_restart", 2048, nullptr, 2, nullptr) != pdPASS) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        httpd_resp_sendstr(request, "Saved; restart the display to complete setup.");
        return ESP_OK;
    }
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_sendstr(request, "Saved. HomeDash is reconnecting to the Bridge.");
    return ESP_OK;
}

static void stop_if_generation(uint32_t generation) {
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_active && s_generation == generation) {
        if (s_server) {
            httpd_stop(s_server);
            s_server = nullptr;
        }
        wifi_stop_recovery_ap();
        s_active = false;
        s_ssid[0] = s_password[0] = 0;
        show_status(false);
    }
    xSemaphoreGive(s_lock);
}

static void expiry_task(void *arg) {
    uint32_t generation = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg));
    vTaskDelay(pdMS_TO_TICKS(kRecoveryWindowMs));
    stop_if_generation(generation);
    vTaskDelete(nullptr);
}
}

bool recovery_ap_start(void) {
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_active) {
        xSemaphoreGive(s_lock);
        return true;
    }
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_ssid, sizeof(s_ssid), "HomeDash-Setup-%02X%02X", mac[4], mac[5]);
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    for (size_t i = 0; i < 16; ++i) s_password[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
    s_password[16] = 0;
    for (size_t i = 0; i < 32; ++i)
        snprintf(&s_csrf_token[i * 2], 3, "%02x", (unsigned)(esp_random() & 0xff));
    if (!wifi_start_recovery_ap(s_ssid, s_password)) {
        xSemaphoreGive(s_lock);
        return false;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 4;
    // Configuration validation performs the same bounded TLS probe as the
    // network task, including its 8 KiB response buffer. Keep equal stack
    // headroom while this short-lived recovery server is active.
    config.stack_size = 32 * 1024;
    if (httpd_start(&s_server, &config) != ESP_OK) {
        wifi_stop_recovery_ap();
        xSemaphoreGive(s_lock);
        return false;
    }
    httpd_uri_t root = {};
    root.uri = "/"; root.method = HTTP_GET; root.handler = page_handler;
    httpd_uri_t setup = {};
    setup.uri = "/config"; setup.method = HTTP_POST; setup.handler = config_handler;
    if (httpd_register_uri_handler(s_server, &root) != ESP_OK ||
        httpd_register_uri_handler(s_server, &setup) != ESP_OK) {
        httpd_stop(s_server);
        s_server = nullptr;
        wifi_stop_recovery_ap();
        xSemaphoreGive(s_lock);
        return false;
    }
    s_active = true;
    const uint32_t generation = ++s_generation;
    show_status(true);
    if (xTaskCreate(expiry_task, "recovery_expiry", 2048,
                    reinterpret_cast<void *>(static_cast<uintptr_t>(generation)), 1, nullptr) != pdPASS) {
        if (s_server) {
            httpd_stop(s_server);
            s_server = nullptr;
        }
        wifi_stop_recovery_ap();
        s_active = false;
        s_ssid[0] = s_password[0] = 0;
        show_status(false);
        xSemaphoreGive(s_lock);
        return false;
    }
    xSemaphoreGive(s_lock);
    return true;
}

void recovery_ap_stop(void) {
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_server) {
        httpd_stop(s_server);
        s_server = nullptr;
    }
    wifi_stop_recovery_ap();
    s_active = false;
    s_ssid[0] = s_password[0] = 0;
    show_status(false);
    xSemaphoreGive(s_lock);
}

bool recovery_ap_active(void) { return s_active; }

#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <new>
#include <string>
#include "cJSON.h"
#include "web_sync.h"
#include "web_identity.h"
#include "web_settings_store.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_tls.h"
#include "freertos/semphr.h"

static bool fail_response_allocation = false;
void *operator new(size_t n, const std::nothrow_t &) noexcept {
    if (fail_response_allocation && n >= 8192) return nullptr;
    try { return ::operator new(n); } catch (...) { return nullptr; }
}
void operator delete(void *p, const std::nothrow_t &) noexcept { ::operator delete(p); }

static const char *device_id = "0123456789abcdef0123456789abcdef";
static const char *bridge_id = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
struct mock_http {
    esp_http_client_config_t config;
    std::map<std::string,std::string> headers;
    std::string url, payload;
};
static mock_http client;
static std::string body;
static int status = 200, requests = 0, cleanups = 0, applies = 0;
static bool transport_error = false, client_oom = false, sync_allowed = true;
static bool expect_stable_status = false;
static bool has_saved = false;
static web_preferences_t saved = {};

const char *esp_err_to_name(esp_err_t e) { return e == ESP_OK ? "ESP_OK" : "ESP_ERR_HTTP_CONNECT"; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c) {
    if (client_oom) return nullptr;
    client = {}; client.config = *c; client.url = c->url;
    assert(c->timeout_ms == 15000 && c->disable_auto_redirect);
    return &client;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h,const char *k,const char *v) { h->headers[k]=v; return ESP_OK; }
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t h,const char *p,int n) { h->payload.assign(p,n); return ESP_OK; }
esp_err_t esp_http_client_perform(esp_http_client_handle_t h) {
    ++requests;
    if (expect_stable_status) {
        char text[96]; web_sync_copy_diagnostic(text, sizeof(text));
        assert(!strcmp(text, "Sync:OK"));
    }
    assert(h->headers["X-Device-ID"] == device_id && h->headers["X-API-Key"] == "private-test-key");
    if (h->config.method == HTTP_METHOD_POST) {
        cJSON *p = cJSON_Parse(h->payload.c_str()); assert(p);
        assert(!strcmp(cJSON_GetObjectItem(p,"device_id")->valuestring, device_id));
        assert(!strcmp(cJSON_GetObjectItem(p,"boot_id")->valuestring,"boot-test"));
        assert(cJSON_GetObjectItem(p,"applied_revision")->valueint == (has_saved ? static_cast<int>(saved.revision) : 0));
        cJSON_Delete(p);
    }
    esp_http_client_event_t e = {}; e.user_data=h->config.user_data;
    if (transport_error) {
        int codes[] = {0x8001, -0x2700, 8}; e.event_id=HTTP_EVENT_ERROR; e.data=codes;
        h->config.event_handler(&e); return ESP_ERR_HTTP_CONNECT;
    }
    e.event_id=HTTP_EVENT_ON_DATA;
    // Deliver bounded chunks to exercise cumulative response overflow.
    for (size_t i=0; i<body.size(); i+=1024) {
        e.data=body.data()+i; e.data_len=static_cast<int>(std::min(size_t(1024),body.size()-i));
        if (h->config.event_handler(&e) != ESP_OK) return ESP_FAIL;
    }
    return ESP_OK;
}
int esp_http_client_get_errno(esp_http_client_handle_t) { return transport_error ? 111 : 0; }
int esp_http_client_get_status_code(esp_http_client_handle_t) { return transport_error ? 0 : status; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t) { ++cleanups; return ESP_OK; }
esp_err_t esp_crt_bundle_attach(void *) { return ESP_OK; }
esp_err_t esp_tls_get_and_clear_last_error(esp_tls_error_handle_t h,int *code,int *flags) {
    auto p=static_cast<int *>(h); *code=p[1]; *flags=p[2]; return p[0];
}
SemaphoreHandle_t xSemaphoreCreateMutex() { return new std::mutex; }
int xSemaphoreTake(SemaphoreHandle_t m,unsigned) { static_cast<std::mutex *>(m)->lock(); return pdTRUE; }
int xSemaphoreGive(SemaphoreHandle_t m) { static_cast<std::mutex *>(m)->unlock(); return pdTRUE; }
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *) { return nullptr; }
esp_err_t esp_netif_get_ip_info(esp_netif_t *,esp_netif_ip_info_t *) { return ESP_FAIL; }
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) { strcpy(reinterpret_cast<char *>(ap->ssid),"private-test-ssid"); return ESP_OK; }
extern "C" int64_t esp_timer_get_time(void) { return 123000000; }
bool wifi_is_connected(void) { return true; }
bool wifi_time_is_valid(void) { return true; }
const char *ota_available_version(void) { return ""; }
bool web_identity_get(char *out,size_t n) { if(n<33)return false; memcpy(out,device_id,33);return true; }
bool web_identity_matches(const char *id) { return id && !strcmp(id,device_id); }
esp_err_t web_identity_set_http_header(esp_http_client_handle_t h) { return esp_http_client_set_header(h,"X-Device-ID",device_id); }
bool web_settings_store_load(web_preferences_t *out) { if(!has_saved)return false; *out=saved; return true; }
bool web_settings_store_save(const web_preferences_t *p) { saved=*p;has_saved=true;return true; }
bool web_settings_store_sync_allowed(void) { return sync_allowed; }
bool web_settings_store_command_result(char *,size_t,char *,size_t,char *,size_t) { return false; }
bool lvgl_port_lock(int) { return true; }
void lvgl_port_unlock(void) {}
void ui_dashboard_apply_preferences(const web_preferences_t *) { ++applies; }
void ui_dashboard_reset_preferences(void) {}
void photo_frame_apply_web_preferences(bool,int,int) {}

static std::string response(const char *id=device_id,const char *scope=bridge_id) {
    return std::string("{\"device_id\":\"")+id+"\",\"bridge_id\":\""+scope+R"(","revision":1,"command":null,"preferences":{
      "screen":{"name":"Hall","city":"Belgrade","timezone":"Europe/Belgrade",
      "clock_format":"24h","idle_action":"photos","night_enabled":false,
      "night_start":"23:00","night_end":"07:00","auto_update_check":true,"auto_install":false},
      "photos":{"enabled":true,"idle_seconds":300,"interval_seconds":60},
      "visible":{"weather":true,"calendar":true,"transport":true,"health":true,"tasks":true,"news":true,"home":true},
      "posix_timezone":"CET-1CEST,M3.5.0,M10.5.0/3","settings_url":"https://bridge.test/settings/"}})";
}
static std::string diagnostic() {
    char text[96]; web_sync_copy_diagnostic(text,sizeof(text));
    assert(!strstr(text,"private-test") && !strstr(text,device_id) && !strstr(text,"https://"));
    return text;
}
static bool poll(web_sync_response_t *out) { return web_sync_poll("https://bridge.test","private-test-key","1.0.22","boot-test",out); }

int main() {
    web_sync_response_t out = {};
    assert(diagnostic()=="Sync:waiting");
    body=response(); assert(poll(&out) && out.has_preferences);
    assert(diagnostic()=="Sync:OK" && web_sync_apply_preferences(&out) && applies==1);
    assert(!strcmp(saved.bridge_id,bridge_id));
    expect_stable_status=true; assert(poll(&out)); expect_stable_status=false;
    body=response("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"); assert(!poll(&out));
    assert(diagnostic()=="Sync:invalid envelope" && applies==1);
    body=response(device_id,"not-a-bridge-id"); assert(!poll(&out));
    saved.revision=3; body=response(); assert(!poll(&out) && applies==1); saved.revision=1;
    body="not JSON"; assert(!poll(&out) && diagnostic()=="Sync:invalid JSON");
    body.assign(8193,'x'); assert(!poll(&out) && diagnostic()=="Sync:response too large");
    body=response(); status=401; assert(!poll(&out) && diagnostic()=="Sync:HTTP 401"); status=200;
    transport_error=true; assert(!poll(&out));
    assert(diagnostic().find("tls=8001/ffffd900/8 e=111")!=std::string::npos); transport_error=false;
    client_oom=true; assert(!poll(&out) && diagnostic()=="Sync:client OOM"); client_oom=false;
    int before=requests; fail_response_allocation=true;
    assert(!poll(&out) && diagnostic()=="Sync:response OOM" && requests==before);
    assert(!web_sync_ack("https://bridge.test","private-test-key","1.0.22","boot-test"));
    assert(!web_sync_send_command_result("https://bridge.test","private-test-key","1.0.22","boot-test","command-test","failed","test_error"));
    char id[33]; assert(!web_sync_probe_bridge("https://bridge.test","private-test-key",id,sizeof(id)));
    fail_response_allocation=false; sync_allowed=false;
    assert(!poll(&out) && diagnostic()=="Sync:payload unavailable" && requests==before); sync_allowed=true;
    body=std::string("{\"version\":1,\"bridge_id\":\"")+bridge_id+"\"}";
    assert(web_sync_probe_bridge("https://bridge.test","private-test-key",id,sizeof(id)) && !strcmp(id,bridge_id));
    body=response(); assert(web_sync_ack("https://bridge.test","private-test-key","1.0.22","boot-test"));
    assert(web_sync_send_command_result("https://bridge.test","private-test-key","1.0.22","boot-test","command-test","failed","test_error"));
    assert(cleanups==requests && applies==1);
    puts("sync client: cold bootstrap, identity, revisions, bounds, TLS diagnostics and OOM passed");
}

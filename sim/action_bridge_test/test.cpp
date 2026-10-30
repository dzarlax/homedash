#include "../../src/bridge.cpp"
#include <cassert>
#include <cstdio>
#include <string>

struct mock_http { esp_http_client_config_t config; } client;
static std::string response_body, posted_body, request_url;
static bool connected=true, init_fail=false, header_fail=false, redirect=false;
static int response_status=200, perform_error=0, calls=0, timeout=0;
static int method=HTTP_METHOD_GET;
static int64_t fake_now=0, stream_delay=0;
extern "C" int64_t esp_timer_get_time() { return fake_now; }
bool wifi_is_connected() { return connected; }
SemaphoreHandle_t xSemaphoreCreateMutex() { return reinterpret_cast<void *>(1); }
int xSemaphoreTake(SemaphoreHandle_t,unsigned) { return 1; }
int xSemaphoreGive(SemaphoreHandle_t) { return 1; }
esp_err_t esp_crt_bundle_attach(void *) { return ESP_OK; }
const char *esp_err_to_name(esp_err_t) { return "safe-test-code"; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    client.config=*config; request_url=config->url;timeout=config->timeout_ms;
    redirect=!config->disable_auto_redirect; method=config->method;
    return init_fail ? nullptr : &client;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t,const char *,const char *) { return header_fail ? ESP_FAIL : ESP_OK; }
esp_err_t web_identity_set_http_header(esp_http_client_handle_t) { return ESP_OK; }
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t,const char *body,int length) { posted_body.assign(body,length); return ESP_OK; }
esp_err_t esp_http_client_perform(esp_http_client_handle_t h) {
    ++calls;
    if (!response_body.empty()) {
        for (size_t pos=0; pos<response_body.size();) {
            size_t length = stream_delay ? 1 : response_body.size();
            fake_now += stream_delay;
            esp_http_client_event_t event={HTTP_EVENT_ON_DATA,h->config.user_data,response_body.data()+pos,static_cast<int>(length)};
            if (h->config.event_handler(&event) != ESP_OK) return ESP_FAIL;
            pos += length;
        }
    }
    return perform_error;
}
int esp_http_client_get_errno(esp_http_client_handle_t) { return 0; }
int esp_http_client_get_status_code(esp_http_client_handle_t) { return response_status; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t) { return ESP_OK; }
static void reset(const std::string &body) {
    response_body=body;posted_body.clear();request_url.clear();connected=true;init_fail=false;header_fail=false;calls=0;perform_error=0;response_status=200;fake_now=0;stream_delay=0;
}
static bridge_action_result_t send() { return bridge_action_request("light.a","turn_on",0,nullptr); }
static std::string observation(const char *entity="light.a",const char *state="on",bool available=true,const char *kind="light") {
    return std::string("{\"entity_id\":\"")+entity+"\",\"kind\":\""+kind+"\",\"available\":"+(available?"true":"false")+",\"state\":\""+state+"\"}";
}
int main() {
    bridge_init("https://bridge.example","test-only-key");
    s_data.light_count=2;s_data.lights_valid=true;
    snprintf(s_data.lights[0].entity_id,sizeof(s_data.lights[0].entity_id),"light.a");
    snprintf(s_data.lights[1].entity_id,sizeof(s_data.lights[1].entity_id),"light.b");s_data.lights[1].on=true;
    reset("{\"ok\":true,\"action_result\":{\"state\":\"accepted\"},\"observation\":"+observation()+"}");
    auto r=send();assert(r.phase==ACTION_ACCEPTED&&r.has_observation&&r.observation.available);
    assert(calls==1&&!redirect&&timeout==10000&&method==HTTP_METHOD_POST);
    assert(posted_body.find("turn_on")!=std::string::npos&&posted_body.find("toggle")==std::string::npos);
    assert(!s_data.lights[0].on); // HTTP parsing alone never commits late results.
    bridge_action_merge_observation(r.observation);
    assert(s_data.light_count==2&&s_data.lights[0].on&&s_data.lights[1].on);
    reset("{\"ok\":true,\"lights\":[{\"id\":\"light.a\",\"on\":false}]}");
    r=send();assert(r.phase==ACTION_ACCEPTED&&!r.has_observation&&s_data.light_count==2&&s_data.lights[0].on);
    reset("{\"observation\":"+observation("light.a","unknown",false)+"}");
    r=bridge_action_read("light.a",800);assert(r.has_observation&&!r.observation.available&&timeout==800&&s_data.lights[0].on);
    for (const auto &o : {observation("light.b"),observation("light.a","unknown",true),observation("light.a","on",true,"climate")}) {
        reset("{\"observation\":"+o+"}");r=bridge_action_read("light.a");assert(!r.has_observation&&r.error==ACTION_ERROR_INVALID_RESPONSE);
    }
    reset("{\"observation\":"+observation("light.a","off")+"}");r=bridge_action_read("light.a",9000);
    assert(s_data.lights[0].on); bridge_action_merge_observation(r.observation);
    assert(r.has_observation&&timeout==3000&&method==HTTP_METHOD_GET&&s_data.light_count==2&&!s_data.lights[0].on&&s_data.lights[1].on);
    assert(request_url=="https://bridge.example/api/ha/state?entity_id=light.a");
    reset("{\"observation\":"+observation()+"}");stream_delay=1000000;
    r=bridge_action_read("light.a",2500);assert(!r.has_observation&&r.error==ACTION_ERROR_TIMEOUT&&calls==1);
    reset("{}");connected=false;r=send();assert(r.phase==ACTION_NOT_SENT&&r.error==ACTION_ERROR_OFFLINE&&calls==0);
    reset("{}");header_fail=true;r=send();assert(r.phase==ACTION_NOT_SENT&&calls==0);
    reset("{}");init_fail=true;r=send();assert(r.phase==ACTION_NOT_SENT&&calls==0);
    reset("{}");perform_error=ESP_ERR_HTTP_CONNECT;r=send();assert(r.phase==ACTION_UNKNOWN&&calls==1);
    reset("{broken");r=send();assert(r.phase==ACTION_UNKNOWN&&r.error==ACTION_ERROR_INVALID_RESPONSE&&calls==1);
    reset("{\"error\":\"raw-secret-and-url\"}");response_status=401;r=send();assert(r.phase==ACTION_REJECTED&&r.error==ACTION_ERROR_UNAUTHORIZED);
    reset("{\"error\":\"timeout\",\"action_result\":{\"state\":\"unknown\",\"error\":\"timeout\"}}");response_status=504;r=send();assert(r.phase==ACTION_UNKNOWN&&r.error==ACTION_ERROR_TIMEOUT);
    reset("{\"error\":\"raw-private-message\"}");response_status=500;r=send();assert(r.phase==ACTION_UNKNOWN&&r.error==ACTION_ERROR_UPSTREAM_UNAVAILABLE);
    reset("{}");response_status=404;r=bridge_action_read("light.a");assert(!r.has_observation&&r.error==ACTION_ERROR_UNSUPPORTED&&calls==1);
    reset(std::string(ACTION_RESPONSE_LIMIT+1,'x'));r=send();assert(r.phase==ACTION_UNKNOWN&&r.error==ACTION_ERROR_INVALID_RESPONSE);
    reset("{}");r=bridge_action_request("light.a?secret=x","turn_on",0,nullptr);assert(r.phase==ACTION_NOT_SENT&&calls==0);
    r=bridge_action_request("light.a","toggle",0,nullptr);assert(r.phase==ACTION_NOT_SENT&&calls==0);
    r=bridge_action_request("climate.a","set_temperature",31,nullptr);assert(r.phase==ACTION_NOT_SENT&&calls==0);
    s_data.climate_count=1;s_data.climate_valid=true;
    snprintf(s_data.climates[0].entity_id,sizeof(s_data.climates[0].entity_id),"climate.a");s_data.climates[0].current_temp=27;s_data.climates[0].has_current_temp=true;
    reset("{\"ok\":true,\"observation\":{\"entity_id\":\"climate.a\",\"kind\":\"climate\",\"available\":true,\"state\":\"cool\",\"target_temperature\":25}}");
    r=bridge_action_request("climate.a","set_temperature",25,nullptr);assert(r.phase==ACTION_ACCEPTED&&r.has_observation&&r.observation.target==25);
    bridge_action_merge_observation(r.observation);
    assert(s_data.climates[0].current_temp==27&&s_data.climates[0].target_temp==25&&s_data.climate_count==1&&s_data.light_count==2);
    puts("PASS: typed action HTTP, explicit intents, bounded responses, legacy acceptance, normalized observations, safe errors and single-entity merge");
}

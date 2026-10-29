#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "ota.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
struct mock_http {esp_http_client_config_t config;};
static mock_http client;
static std::string body;
static int status=200, pauses=0,resumes=0,flashes=0,reboots=0;
static bool connected=true,may_pause=true,paused=false;
bool wifi_is_connected(){return connected;}
bool photo_frame_pause_for_ota(){++pauses;paused=may_pause;return may_pause;}
void photo_frame_resume_after_ota(){++resumes;paused=false;}
void esp_restart(){++reboots;}
esp_err_t esp_crt_bundle_attach(void*){return ESP_OK;}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c){client.config=*c;return &client;}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t,const char*,const char*){return ESP_OK;}
int esp_http_client_get_status_code(esp_http_client_handle_t){return status;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t){return ESP_OK;}
esp_err_t esp_http_client_perform(esp_http_client_handle_t h){
 esp_http_client_event_t e={};e.event_id=HTTP_EVENT_ON_DATA;e.user_data=h->config.user_data;e.data=body.data();e.data_len=body.size();return h->config.event_handler(&e);
}
esp_err_t esp_https_ota(const esp_https_ota_config_t*){assert(paused);++flashes;return ESP_FAIL;}
static void reset(const char *response){body=response;status=200;pauses=resumes=flashes=reboots=0;connected=true;may_pause=true;paused=false;}
int main(){
 ota_init("https://bridge.test","test-key");
 for(const char *response:{"{\"update\":false}","not json"}){reset(response);ota_check_and_update();assert(pauses==0&&resumes==0&&flashes==0);}
 reset("{\"update\":true,\"version\":\"1.0.21\"}");status=503;ota_check_and_update();assert(pauses==0&&flashes==0);
 reset("{\"update\":true,\"version\":\"1.0.21\"}");may_pause=false;ota_check_and_update();assert(pauses==1&&flashes==0);
 reset("{\"update\":true,\"version\":\"1.0.21\"}");ota_check_and_update();assert(pauses==1&&flashes==1&&resumes==1&&!paused);
 reset("{\"update\":true}");connected=false;ota_check_and_update();assert(pauses==0&&flashes==0);
 std::puts("PASS: OTA checks preserve photo mode; only flashing pauses it; failed pause blocks flashing; failed flash resumes");
}

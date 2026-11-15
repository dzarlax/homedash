#include "voice_pair_runtime.h"
#include "nvs.h"
#include "esp_http_client.h"
#include <curl/curl.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using Blob=std::vector<unsigned char>;
static std::map<std::string,Blob> disk,pending;
extern "C" esp_err_t nvs_open(const char*,int mode,nvs_handle_t *h){if(mode==NVS_READONLY&&disk.empty())return ESP_ERR_NVS_NOT_FOUND;*h=1;return ESP_OK;}
extern "C" esp_err_t nvs_get_blob(nvs_handle_t,const char *k,void *b,size_t *n){auto i=disk.find(k);if(i==disk.end())return ESP_ERR_NVS_NOT_FOUND;if(!b){*n=i->second.size();return 0;}if(*n<i->second.size())return ESP_FAIL;*n=i->second.size();memcpy(b,i->second.data(),*n);return 0;}
extern "C" esp_err_t nvs_set_blob(nvs_handle_t,const char *k,const void *b,size_t n){pending[k]=Blob((const unsigned char*)b,(const unsigned char*)b+n);return 0;}
extern "C" esp_err_t nvs_erase_key(nvs_handle_t,const char *k){return disk.erase(k)?0:ESP_ERR_NVS_NOT_FOUND;}
extern "C" esp_err_t nvs_commit(nvs_handle_t){for(auto &p:pending)disk[p.first]=p.second;pending.clear();return 0;}
extern "C" void nvs_close(nvs_handle_t){pending.clear();}
int64_t esp_timer_get_time(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
bool wifi_is_connected(){return true;} bool wifi_pairing_time_is_valid(){return true;} int esp_crt_bundle_attach(void*){return 0;}
static const char *ca;
struct mock_http_client {esp_http_client_config_t cfg;std::string url,body;long status=0;};
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c){if(!c||!c->crt_bundle_attach||c->skip_cert_common_name_check||!c->disable_auto_redirect||c->timeout_ms>2000)return nullptr;auto p=new mock_http_client{};p->cfg=*c;p->url=c->url;return p;}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t,const char*,const char*){return 0;}
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t c,const char *b,int n){c->body.assign(b,n);return 0;}
static size_t receive(char *p,size_t s,size_t n,void *v){auto c=static_cast<mock_http_client*>(v);esp_http_client_event_t ev{HTTP_EVENT_ON_DATA,c->cfg.user_data,p,(int)(s*n)};return c->cfg.event_handler(&ev)==ESP_OK?s*n:0;}
esp_err_t esp_http_client_perform(esp_http_client_handle_t c){CURL *h=curl_easy_init();if(!h)return ESP_FAIL;char errbuf[CURL_ERROR_SIZE]{};curl_easy_setopt(h,CURLOPT_ERRORBUFFER,errbuf);struct curl_slist *headers=nullptr;headers=curl_slist_append(headers,"Content-Type: application/json");headers=curl_slist_append(headers,"Accept-Encoding: identity");curl_easy_setopt(h,CURLOPT_URL,c->url.c_str());curl_easy_setopt(h,CURLOPT_CAINFO,ca);curl_easy_setopt(h,CURLOPT_PROXY,"");curl_easy_setopt(h,CURLOPT_TIMEOUT_MS,2000L);curl_easy_setopt(h,CURLOPT_FOLLOWLOCATION,0L);curl_easy_setopt(h,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(h,CURLOPT_SSL_VERIFYHOST,2L);curl_easy_setopt(h,CURLOPT_HTTPHEADER,headers);curl_easy_setopt(h,CURLOPT_POSTFIELDS,c->body.c_str());curl_easy_setopt(h,CURLOPT_POSTFIELDSIZE,(long)c->body.size());curl_easy_setopt(h,CURLOPT_WRITEFUNCTION,receive);curl_easy_setopt(h,CURLOPT_WRITEDATA,c);auto r=curl_easy_perform(h);curl_easy_getinfo(h,CURLINFO_RESPONSE_CODE,&c->status);fprintf(stderr,"HTTP fixture path=%s curl=%d status=%ld\n",c->url.find("/challenge")!=std::string::npos?"challenge":"proof",(int)r,c->status);if(r!=CURLE_OK)fprintf(stderr,"TLS error: %s\n",errbuf);curl_slist_free_all(headers);curl_easy_cleanup(h);return r==CURLE_OK?ESP_OK:ESP_FAIL;}
int esp_http_client_get_status_code(esp_http_client_handle_t c){return (int)c->status;} esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c){delete c;return 0;}
int main(int argc,char **argv){if(argc!=4)return 2;ca=argv[2];uint8_t bridge[16],device[16];if(!voice_pair::decode_id(argv[3],32,bridge))return 3;memset(device,0xaa,16);voice_pair::EntropyReady ready{true,true};voice_pair_runtime::Session session;auto now=(uint64_t)esp_timer_get_time()/1000;if(!session.begin(bridge,device,argv[1],strlen(argv[1]),now,ready))return 4;char qr[200]{};if(!session.copy_qr(qr,sizeof(qr),argv[1],strlen(argv[1]),now,ready))return 5;printf("%s\n",qr);fflush(stdout);char signal[16];if(!fgets(signal,sizeof(signal),stdin))return 6;std::atomic<bool> cancel{false};if(session.poll(argv[1],strlen(argv[1]),cancel)!=voice_pair_runtime::Status::AwaitingAck)return 7;if(session.poll(argv[1],strlen(argv[1]),cancel)!=voice_pair_runtime::Status::Complete)return 8;voice_pair_store::State state{};if(voice_pair_store::load(state,ready)!=voice_pair_store::Result::Ok||!state.has_active)return 9;printf("%s\n",state.active.token);fflush(stdout);voice_pair_store::wipe(&state,sizeof(state));
 char command[2048]{}; if(!fgets(command,sizeof(command),stdin))return 10; size_t len=strlen(command); if(len && command[len-1]=='\n')--len;
 voice_pair::Clear clear{}; if(!voice_pair_runtime::parse_clear(command,len,clear)||!voice_pair_runtime::verify_clear_command(clear,ready))return 11;
 auto tampered=clear; tampered.signature[0]^=1; if(voice_pair_runtime::verify_clear_command(tampered,ready))return 12;
 if(voice_pair_runtime::persist_clear(clear,false,ready)||voice_pair_runtime::acknowledge_clear(false,ready)!=voice_pair_runtime::ClearAckResult::Pending)return 13;
 if(!voice_pair_runtime::persist_clear(clear,true,ready))return 14;
 if(voice_pair_store::load(state,ready)!=voice_pair_store::Result::Ok||!state.has_tombstone||state.has_active||state.active.token[0]||state.clear_acknowledged)return 15;
 voice_pair_store::wipe(&state,sizeof(state));
 voice_pair_store::test_reboot(); // Keep synthetic NVS, reset process-local journal poison state.
 if(voice_pair_runtime::acknowledge_clear(true,ready)!=voice_pair_runtime::ClearAckResult::Done)return 16;
 if(voice_pair_store::load(state,ready)!=voice_pair_store::Result::Ok||!state.has_tombstone||!state.clear_acknowledged||state.active.token[0])return 17;
 voice_pair_store::wipe(&state,sizeof(state));voice_pair::wipe(&clear,sizeof(clear));voice_pair::wipe(command,sizeof(command));puts("clear_ack_done");fflush(stdout);return 0;}

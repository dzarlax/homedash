#include "voice_config_store.h"
#include "nvs.h"
#include <cstring>
bool voice_config_store_save(const char *base, const char *token) {
    if (!voice_config_valid(base,token)) return false;
    VoiceProvision p={}; p.version=1;
    memcpy(p.base_url,base,strlen(base)+1); memcpy(p.token,token,strlen(token)+1);
    nvs_handle_t h; esp_err_t err=nvs_open("voice_push",NVS_READWRITE,&h);
    if (err==ESP_OK) { err=nvs_set_blob(h,"config",&p,sizeof(p)); if (err==ESP_OK) err=nvs_commit(h); nvs_close(h); }
    voice_secure_zero(&p,sizeof(p)); return err==ESP_OK;
}
bool voice_config_store_load(VoiceProvision &out) {
    voice_secure_zero(&out,sizeof(out));
    nvs_handle_t h; if (nvs_open("voice_push",NVS_READONLY,&h)!=ESP_OK) return false;
    VoiceProvision p={}; size_t n=sizeof(p);
    bool ok=nvs_get_blob(h,"config",&p,&n)==ESP_OK && n==sizeof(p) && p.version==1 &&
        memchr(p.base_url,0,sizeof(p.base_url)) && memchr(p.token,0,sizeof(p.token)) &&
        voice_config_valid(p.base_url,p.token);
    nvs_close(h); if (ok) out=p; voice_secure_zero(&p,sizeof(p)); return ok;
}
bool voice_config_store_clear() {
    nvs_handle_t h; esp_err_t err=nvs_open("voice_push",NVS_READWRITE,&h);
    if (err==ESP_OK) { err=nvs_erase_all(h); if (err==ESP_OK) err=nvs_commit(h); nvs_close(h); }
    return err==ESP_OK;
}

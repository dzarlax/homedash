#include "voice_config_store.h"
#include "nvs.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
static std::vector<unsigned char> persisted;
static bool fail_write=false, fail_commit=false;
int nvs_open(const char *ns,int,nvs_handle_t *h) { assert(!strcmp(ns,"voice_push")); *h=1; return ESP_OK; }
int nvs_set_blob(nvs_handle_t,const char *key,const void *data,size_t n) { assert(!strcmp(key,"config")); if(fail_write)return -1; const auto *p=static_cast<const unsigned char*>(data); persisted.assign(p,p+n); return ESP_OK; }
int nvs_get_blob(nvs_handle_t,const char *key,void *data,size_t *n) { assert(!strcmp(key,"config")); if(persisted.empty() || *n<persisted.size())return -1; *n=persisted.size(); memcpy(data,persisted.data(),*n); return ESP_OK; }
int nvs_commit(nvs_handle_t) { if(fail_commit)return -1; return ESP_OK; }
int nvs_erase_all(nvs_handle_t) { persisted.clear(); return ESP_OK; }
void nvs_close(nvs_handle_t) {}
int main() {
    VoiceProvision p={}; assert(!voice_config_store_load(p));
    assert(voice_config_store_save("wss://example.test","synthetic-one"));
    assert(voice_config_store_load(p) && !strcmp(p.token,"synthetic-one"));
    fail_write=true; assert(!voice_config_store_save("wss://example.test","synthetic-two")); fail_write=false;
    assert(voice_config_store_load(p) && !strcmp(p.token,"synthetic-one"));
    fail_commit=true; assert(!voice_config_store_clear()); fail_commit=false;
    // IDF 5.3 writes before commit; an error at commit has an ambiguous outcome.
    assert(!voice_config_store_load(p) && p.token[0]==0);
    assert(voice_config_store_save("wss://example.test","synthetic-one"));
    assert(voice_config_store_clear()); assert(!voice_config_store_load(p));
    std::cout<<"voice NVS store fixture passed\n";
}

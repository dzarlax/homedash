#include "voice_push_client.h"
#include "voice_protocol.h"
#include "voice_backoff.h"
#include "voice_startup.h"
#include "voice_config.h"
#ifndef SIMULATOR
#include "voice_config_store.h"
#endif
#include "voice_fragments.h"
#include "ui_voice.h"
#include "ui_dashboard.h"
#include "photo_frame.h"
#include "lvgl_lock.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#ifndef SIMULATOR
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "heap_diagnostics.h"
 #include "esp_websocket_client.h"
#include "nvs.h"
#endif
#include <atomic>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <new>

namespace {
std::atomic<SemaphoreHandle_t> lock{nullptr};
voice::ViewModel *model;
voice::Fragments fragments;
std::atomic<uint32_t> priorities{0};
std::atomic<bool> finished{false};
std::atomic<bool> disabled{false};
uint32_t generation=1, rendered=0;
bool on_screen=false;
std::atomic<bool> connected_once{false};
std::atomic<uint64_t> connected_since_ms{0};
char endpoint[192], header[280];
#ifdef SIMULATOR
uint64_t time_offset_ms=0;
int sim_startup_mode=0;
unsigned sim_allocations=0, sim_constructed=0, sim_destroyed=0, sim_frees=0, sim_mutexes=0, sim_workers=0, sim_clients=0;
#endif
uint64_t now_ms() {
    uint64_t now=(uint64_t)esp_timer_get_time()/1000;
#ifdef SIMULATOR
    now+=time_offset_ms;
#endif
    return now;
}
void dismiss(void *) { xSemaphoreTake(lock,portMAX_DELAY); model->dismiss(); ++generation; xSemaphoreGive(lock); }
void activity(void *) { xSemaphoreTake(lock,portMAX_DELAY); model->touch(now_ms()); xSemaphoreGive(lock); }
ui_voice_phase_t phase(voice::Phase p) {
    switch(p) { case voice::Phase::Listening:return UI_VOICE_LISTENING; case voice::Phase::Processing:return UI_VOICE_PROCESSING; case voice::Phase::Lookup:return UI_VOICE_LOOKUP; case voice::Phase::Answer:return UI_VOICE_ANSWER; case voice::Phase::ApprovalRequired:return UI_VOICE_APPROVAL_REQUIRED; default:return UI_VOICE_ERROR; }
}
void display_update(lv_timer_t *) {
    if (!lock) return;
    xSemaphoreTake(lock,portMAX_DELAY);
    bool old_visible=model->visible(), old_offline=model->offline();
    model->tick(now_ms());
    if (priorities.load()) model->block();
    if (old_visible!=model->visible() || old_offline!=model->offline()) ++generation;
    bool visible=model->visible(), offline=model->offline();
    uint32_t current_generation=generation;
    voice::Snapshot snapshot;
    if (visible) snapshot=model->snapshot();
    xSemaphoreGive(lock);
    // Never hold the model mutex while calling LVGL: scroll callbacks can
    // synchronously report activity and need the same mutex.
    if (visible && !on_screen) {
        ui_dashboard_hide_sensitive();
        photo_frame_set_voice_active(true);
        ui_dashboard_set_voice_active(true);
        ui_voice_state_t v={offline?UI_VOICE_OFFLINE:phase(snapshot.phase),snapshot.text.c_str(),snapshot.interrupted,snapshot.text_truncated};
        ui_voice_show(&v); on_screen=true; rendered=current_generation;
    } else if (!visible && on_screen) {
        if (priorities.load()) ui_voice_hide_immediately(); else ui_voice_hide();
        photo_frame_set_voice_active(false); ui_dashboard_set_voice_active(false); on_screen=false; rendered=current_generation;
    } else if (visible && rendered!=current_generation) {
        ui_voice_state_t v={offline?UI_VOICE_OFFLINE:phase(snapshot.phase),snapshot.text.c_str(),snapshot.interrupted,snapshot.text_truncated};
        ui_voice_update(&v); rendered=current_generation;
    }
    std::fill(snapshot.text.begin(),snapshot.text.end(),'\0');
}
void receive(const char *data, size_t len, size_t offset, size_t total, uint8_t opcode, bool fin) {
    if (opcode==10) { xSemaphoreTake(lock,portMAX_DELAY); model->heartbeat(now_ms()); xSemaphoreGive(lock); return; }
    if (opcode>=8) return;
    auto r=fragments.add(data,len,offset,total,opcode,fin);
    if (r!=voice::Fragments::Result::Complete) return;
    voice::Message message; bool valid=voice::parse_message(fragments.message().data(),fragments.message().size(),message); fragments.reset();
    if (!valid) return;
    xSemaphoreTake(lock,portMAX_DELAY);
    model->heartbeat(now_ms());
    bool changed=message.clear ? model->clear(message.snapshot.lease,message.snapshot.revision) : model->apply(message.snapshot,now_ms(),priorities.load()!=0,message.replay ? voice::Delivery::Replay : voice::Delivery::Live);
    if (changed) ++generation;
    xSemaphoreGive(lock);
    std::fill(message.snapshot.text.begin(),message.snapshot.text.end(), '\0');
}
#ifndef SIMULATOR
void event(void *, esp_event_base_t, int32_t id, void *data) {
    if (id==WEBSOCKET_EVENT_CONNECTED) {
        log_heap_snapshot("voice_connected");
        connected_since_ms.store(now_ms()); connected_once.store(true); fragments.reset(); xSemaphoreTake(lock,portMAX_DELAY); model->connected(now_ms()); ++generation; xSemaphoreGive(lock);
    } else if (id==WEBSOCKET_EVENT_DISCONNECTED || id==WEBSOCKET_EVENT_CLOSED) {
        fragments.reset(); xSemaphoreTake(lock,portMAX_DELAY); model->disconnect(now_ms()); ++generation; xSemaphoreGive(lock);
    } else if (id==WEBSOCKET_EVENT_FINISH) { finished.store(true); }
    else if (id==WEBSOCKET_EVENT_DATA) {
        auto *e=static_cast<esp_websocket_event_data_t *>(data);
        if (e->data_len<0 || e->payload_len<0 || e->payload_offset<0) { fragments.reset(); return; }
        receive(e->data_ptr,(size_t)e->data_len,(size_t)e->payload_offset,(size_t)e->payload_len,e->op_code,e->fin);
    }
}
bool load_config() {
    VoiceProvision provision={}; bool ok=voice_config_store_load(provision);
    int a=0,b=0;
    if (ok) {
        a=snprintf(endpoint,sizeof(endpoint),"%s/api/voice/v1/events",provision.base_url);
        b=snprintf(header,sizeof(header),"Authorization: Bearer %s\r\n",provision.token);
    }
    voice_secure_zero(&provision,sizeof(provision));
    return ok && a>0 && (size_t)a<sizeof(endpoint) && b>0 && (size_t)b<sizeof(header);
}
void worker(void *) {
    unsigned delay_sec=1;
    while (!disabled.load()) {
        if (!wifi_is_connected() || !wifi_time_is_valid()) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
        esp_websocket_client_config_t cfg={}; cfg.uri=endpoint; cfg.headers=header; cfg.crt_bundle_attach=esp_crt_bundle_attach;
        cfg.skip_cert_common_name_check=false; cfg.disable_auto_reconnect=true; cfg.ping_interval_sec=15; cfg.pingpong_timeout_sec=30;
        cfg.buffer_size=2048; cfg.task_stack=8192; cfg.network_timeout_ms=10000;
        auto c=esp_websocket_client_init(&cfg);
        finished.store(false); connected_once.store(false);
        if (c) {
            bool registered = esp_websocket_register_events(c,WEBSOCKET_EVENT_ANY,event,nullptr)==ESP_OK;
            if (registered) log_heap_snapshot("voice_before_start");
            if (registered && esp_websocket_client_start(c)==ESP_OK) {
                while (!finished.load() && !disabled.load()) vTaskDelay(pdMS_TO_TICKS(200));
                if (disabled.load() && !finished.load()) esp_websocket_client_stop(c);
            }
            esp_websocket_client_destroy(c);
        }
        xSemaphoreTake(lock,portMAX_DELAY); model->disconnect(now_ms()); ++generation; xSemaphoreGive(lock);
        if (disabled.load()) break;
        bool was_connected=connected_once.load();
        uint64_t connected_for=was_connected ? now_ms()-connected_since_ms.load() : 0;
        delay_sec=voice_reconnect_after_session(delay_sec,was_connected,connected_for);
        vTaskDelay(pdMS_TO_TICKS(voice_reconnect_delay_ms(delay_sec,esp_random())));
        delay_sec=voice_reconnect_next(delay_sec);
    }
    voice_secure_zero(header,sizeof(header));
    vTaskDelete(nullptr);
}
#endif
}
void voice_push_ui_init() {
    static void *storage = nullptr;
    const VoiceStartupOps ops = {
#ifndef SIMULATOR
        [] { return load_config(); },
        [] {
            storage=heap_caps_malloc(sizeof(voice::ViewModel), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (storage) ESP_LOGI("voice_push", "model bytes=%u external=%u",
                                  (unsigned)sizeof(voice::ViewModel), (unsigned)esp_ptr_external_ram(storage));
            return storage != nullptr;
        },
#else
        [] { if (sim_startup_mode==1) return false; if (sim_startup_mode==2) { std::snprintf(header,sizeof(header),"partial fixture"); return voice_config_valid("http://invalid", "token"); } std::snprintf(header,sizeof(header),"fixture"); return true; },
        [] { ++sim_allocations; if (sim_startup_mode==3) return false; storage=::operator new(sizeof(voice::ViewModel),std::nothrow); return storage != nullptr; },
#endif
        [] { model=new (storage) voice::ViewModel();
#ifdef SIMULATOR
            ++sim_constructed;
#endif
        },
        [] {
#ifdef SIMULATOR
            ++sim_mutexes; if (sim_startup_mode==4) return false;
#endif
            lock=xSemaphoreCreateMutex(); return lock.load()!=nullptr;
        },
        [] { model->~ViewModel(); model=nullptr;
#ifdef SIMULATOR
            ++sim_destroyed;
#endif
        },
        [] {
#ifndef SIMULATOR
            heap_caps_free(storage);
#else
            ::operator delete(storage); ++sim_frees;
#endif
            storage=nullptr;
        },
        [] { voice_secure_zero(header,sizeof(header)); }
    };
    if (!voice_initialize(ops)) return;
    ui_voice_create(lv_screen_active(),dismiss,activity,nullptr); lv_timer_create(display_update,100,nullptr);
}
#ifndef SIMULATOR
bool voice_push_provision(const char *base, const char *token) {
    if (!voice_config_valid(base,token)) return false;
    bool saved=voice_config_store_save(base,token);
    // A failed commit can still have written the blob; never resume the old
    // subscription after an ambiguous replacement attempt.
    disabled.store(true); voice_push_priority(VOICE_PRIORITY_RECOVERY, true);
    return saved;
}
bool voice_push_forget() {
    disabled.store(true);
    voice_push_priority(VOICE_PRIORITY_RECOVERY, true);
    return voice_config_store_clear();
}
#else
bool voice_push_provision(const char *base, const char *token) { return voice_config_valid(base,token); }
bool voice_push_forget() { return true; }
#endif
void voice_push_touch() { if (lock) { xSemaphoreTake(lock,portMAX_DELAY); model->touch(now_ms()); xSemaphoreGive(lock); } }
void voice_push_start() {
    const VoiceStartOps ops = {
        [] { return lock.load()!=nullptr; },
#ifndef SIMULATOR
        [] {
            // The transport can otherwise log the bearer during failed handshakes.
            esp_log_level_set("transport_ws", ESP_LOG_NONE);
            return esp_log_level_get("transport_ws") == ESP_LOG_NONE;
        },
        [] {
            bool ok=xTaskCreatePinnedToCore(worker,"voice_push",4096,nullptr,1,nullptr,0)==pdPASS;
            if (!ok) ESP_LOGE("voice_push", "Worker creation failed");
            return ok;
        },
#else
        [] { return true; },
        [] { ++sim_workers; if (sim_startup_mode==5) return false; ++sim_clients; return true; },
#endif
        [] { voice_secure_zero(header,sizeof(header)); }
    };
    voice_start_optional(ops);
}
void voice_push_priority(VoicePriority reason,bool enabled) {
    if (enabled) priorities.fetch_or(reason); else priorities.fetch_and(~(uint32_t)reason);
    if (enabled && lock && lvgl_port_lock(-1)) {
        display_update(nullptr);
        // A dismissed overlay may still be fading after on_screen was cleared.
        // Priority modes must never remain covered, even for those 180 ms.
        ui_voice_hide_immediately();
        lvgl_port_unlock();
    }
}

#ifdef SIMULATOR
void voice_push_sim_startup_mode(int mode) { sim_startup_mode=mode; }
void voice_push_sim_startup_stats(unsigned *out) {
    out[0]=sim_allocations; out[1]=sim_constructed; out[2]=sim_destroyed; out[3]=sim_frees;
    out[4]=sim_mutexes; out[5]=sim_workers; out[6]=sim_clients; out[7]=header[0] != 0;
    out[8]=lock.load()!=nullptr;
}
void voice_push_sim_connected() { fragments.reset(); xSemaphoreTake(lock,portMAX_DELAY); model->connected(now_ms()); ++generation; xSemaphoreGive(lock); }
void voice_push_sim_disconnected() { fragments.reset(); xSemaphoreTake(lock,portMAX_DELAY); model->disconnect(now_ms()); ++generation; xSemaphoreGive(lock); }
void voice_push_sim_advance(uint64_t ms) { time_offset_ms+=ms; }
void voice_push_sim_frame(const char *p,unsigned n,unsigned off,unsigned total,unsigned opcode,bool fin) { receive(p,n,off,total,(uint8_t)opcode,fin); }
void voice_push_sim_refresh() { display_update(nullptr); }
void voice_push_sim_dismiss() { dismiss(nullptr); }
#endif

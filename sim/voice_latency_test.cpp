#include "ui_dashboard.h"
#include "voice_push_client.h"
#include "ui_voice.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
int main() {
    lv_init(); lv_display_create(1024,600); ui_dashboard_create(); voice_push_ui_init(); voice_push_sim_connected();
    std::vector<double> samples;
    for (int i=0;i<40;++i) {
        std::string s="{\"v\":1,\"type\":\"voice.snapshot\",\"lease_id\":\"lease\",\"revision\":"+std::to_string(i+1)+",\"session_id\":\"session\",\"turn_id\":\"turn-"+std::to_string(i)+"\",\"phase\":\"listening\",\"backend_active\":false,\"playback\":\"unknown\",\"text\":\"\",\"text_truncated\":false,\"interrupted\":false,\"active\":true,\"ttl_ms\":60000,\"replay\":false}";
        auto start=std::chrono::steady_clock::now();
        voice_push_sim_frame(s.data(),s.size(),0,s.size(),1,true); voice_push_sim_refresh();
        assert(ui_voice_is_visible());
        auto end=std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double,std::milli>(end-start).count());
        voice_push_sim_dismiss(); voice_push_sim_refresh();
    }
    std::sort(samples.begin(),samples.end()); double p95=samples[37];
    std::printf("synthetic in-process receive-to-LVGL p95: %.3f ms (40 samples; no network or display flush)\n",p95);
    assert(p95<500.0);
}

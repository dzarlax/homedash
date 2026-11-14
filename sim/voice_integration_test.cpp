#include "ui_dashboard.h"
#include "ui_voice.h"
#include "voice_push_client.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <cstdlib>
static std::string snapshot(int rev,const char *turn,const char *phase,const char *text,bool replay=false) {
    return "{\"v\":1,\"type\":\"voice.snapshot\",\"lease_id\":\"lease\",\"revision\":"+std::to_string(rev)+",\"session_id\":\"session\",\"turn_id\":\""+turn+"\",\"phase\":\""+phase+"\",\"backend_active\":false,\"playback\":\"unknown\",\"text\":\""+text+"\",\"text_truncated\":false,\"interrupted\":false,\"active\":true,\"ttl_ms\":150000,\"replay\":"+(replay?"true":"false")+"}";
}
static void feed(const std::string &s) {
    const unsigned n=s.size(), half=n/2;
    voice_push_sim_frame(s.data(),half,0,half,1,false);
    voice_push_sim_frame(s.data()+half,n-half,0,n-half,0,true);
    voice_push_sim_refresh();
}
int main() {
    lv_init(); lv_display_create(1024,600); ui_dashboard_create();
    ui_dashboard_sim_open_reader(); lv_obj_update_layout(lv_screen_active());
    int32_t pos=ui_dashboard_sim_reader_scroll_y();
    voice_push_ui_init(); voice_push_sim_connected();
    feed(snapshot(1,"one","listening","")); assert(ui_voice_is_visible());
    feed(snapshot(2,"one","answer","Привет")); assert(strstr(ui_voice_sim_answer_text(),"Привет"));
    voice_push_sim_dismiss(); voice_push_sim_refresh(); assert(!ui_voice_is_visible() && !*ui_voice_sim_answer_text());
    assert(!ui_voice_sim_physically_hidden());
    voice_push_priority(VOICE_PRIORITY_OTA,true); assert(ui_voice_sim_physically_hidden());
    voice_push_priority(VOICE_PRIORITY_OTA,false);
    feed(snapshot(3,"one","answer","Секрет")); assert(!ui_voice_is_visible());
    assert(ui_dashboard_reader_open() && ui_dashboard_sim_reader_scroll_y()==pos);
    voice_push_priority(VOICE_PRIORITY_OTA,true);
    feed(snapshot(4,"two","listening","")); assert(!ui_voice_is_visible());
    voice_push_priority(VOICE_PRIORITY_OTA,false);
    feed(snapshot(5,"two","answer","Не показывать")); assert(!ui_voice_is_visible());
    feed(snapshot(6,"three","listening","")); assert(ui_voice_is_visible());
    voice_push_priority(VOICE_PRIORITY_RECOVERY,true); assert(!ui_voice_is_visible() && !*ui_voice_sim_answer_text());
    voice_push_priority(VOICE_PRIORITY_RECOVERY,false);
    assert(ui_dashboard_reader_open() && ui_dashboard_sim_reader_scroll_y()==pos);
    feed(snapshot(7,"four","listening",""));
    feed(snapshot(8,"four","answer","private"));
    voice_push_sim_disconnected(); voice_push_sim_advance(30000); voice_push_sim_refresh();
    assert(ui_voice_is_visible() && !*ui_voice_sim_answer_text());
    assert(!strcmp(ui_voice_sim_status_text(),"Связь прервана"));
    voice_push_sim_connected(); feed(snapshot(9,"four","answer","private"));
    assert(!ui_voice_is_visible());
    feed(snapshot(10,"five","listening","session speech")); assert(ui_voice_is_visible());
    // The established stream never received turn six's listening snapshot.
    feed(snapshot(15,"six","lookup","session speech"));
    assert(ui_voice_is_visible() && strstr(ui_voice_sim_answer_text(),"session speech"));
    voice_push_priority(VOICE_PRIORITY_OTA,true); voice_push_priority(VOICE_PRIORITY_OTA,false);
    feed(snapshot(16,"six","lookup","session speech")); assert(!ui_voice_is_visible());
    feed(snapshot(17,"seven","lookup","session speech",true)); assert(!ui_voice_is_visible());
    feed(snapshot(18,"seven","lookup","session speech")); assert(!ui_voice_is_visible());
    if (const char *seconds_env=std::getenv("VOICE_SOAK_SECONDS")) {
        int seconds=std::atoi(seconds_env); auto begin=std::chrono::steady_clock::now();
        for (int i=0;i<seconds;++i) {
            std::string turn="soak-"+std::to_string(i); int rev=20+i*2;
            if (i%5==0) voice_push_sim_connected();
            feed(snapshot(rev,turn.c_str(),"listening",""));
            assert(ui_voice_is_visible());
            feed(snapshot(rev+1,turn.c_str(),"answer","Synthetic text"));
            if (i%7==0) { voice_push_priority(VOICE_PRIORITY_OTA,true); voice_push_priority(VOICE_PRIORITY_OTA,false); }
            else { voice_push_sim_dismiss(); voice_push_sim_refresh(); }
            assert(!ui_voice_is_visible());
            lv_tick_inc(1000); lv_timer_handler();
            std::this_thread::sleep_until(begin+std::chrono::seconds(i+1));
        }
        std::cout<<"mixed UI soak passed: "<<seconds<<" seconds\n";
    }
    std::cout<<"voice integration test passed\n";
}

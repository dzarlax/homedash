#include "voice_push_client.h"
#include "ui_dashboard.h"
#include "ui_voice.h"
#include <cassert>
#include <cstring>
#include <cstdio>

int main(int argc, char **) {
    if (argc>1) { voice_push_request_stop(); assert(voice_push_is_stopped()); return 0; }
    lv_init(); lv_display_create(1024,600); ui_dashboard_create();
    voice_push_ui_init(); voice_push_start(); voice_push_start();
    unsigned stats[9]{}; voice_push_sim_startup_stats(stats);
    assert(stats[5]==1 && stats[6]==1 && stats[7]==1);
    voice_push_sim_connected();
    const char frame[]="{\"v\":1,\"type\":\"voice.snapshot\",\"lease_id\":\"lease\",\"revision\":1,\"session_id\":\"session\",\"turn_id\":\"one\",\"phase\":\"answer\",\"backend_active\":false,\"playback\":\"unknown\",\"text\":\"secret\",\"text_truncated\":false,\"interrupted\":false,\"active\":true,\"ttl_ms\":150000,\"replay\":false}";
    voice_push_sim_frame(frame,strlen(frame),0,strlen(frame),1,true);
    voice_push_sim_refresh(); assert(ui_voice_is_visible());
    voice_push_request_stop();
    assert(!ui_voice_is_visible() && ui_voice_sim_physically_hidden());
    assert(!voice_push_is_stopped()); // In-flight connection has not been destroyed.
    voice_push_sim_finish_worker(false); // Destroy failed or has not completed by caller deadline.
    assert(!voice_push_is_stopped());
    voice_push_sim_startup_stats(stats); assert(!stats[7]); // Header wiped despite failure.
    voice_push_sim_finish_worker(true);
    assert(voice_push_is_stopped());
    voice_push_start(); voice_push_sim_startup_stats(stats); assert(stats[5]==1);
    std::puts("voice stop lifecycle passed");
}

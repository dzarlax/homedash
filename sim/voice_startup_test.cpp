#include "voice_push_client.h"
#include "ui_voice.h"
#include "lvgl.h"
#include <cassert>
#include <cstdlib>
#include <cstdio>

static unsigned timer_count() {
    unsigned n=0;
    for (lv_timer_t *t=lv_timer_get_next(nullptr); t; t=lv_timer_get_next(t)) ++n;
    return n;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    const int mode = std::atoi(argv[1]);
    lv_init();
    lv_display_create(1024, 600);
    const auto children_before=lv_obj_get_child_count(lv_screen_active());
    const auto timers_before=timer_count();
    voice_push_sim_startup_mode(mode);
    voice_push_ui_init();
    voice_push_touch();
    voice_push_priority(VOICE_PRIORITY_OTA, true);
    voice_push_priority(VOICE_PRIORITY_OTA, false);
    voice_push_start();
    unsigned s[9] = {};
    voice_push_sim_startup_stats(s);
    if (mode >= 1 && mode <= 4) {
        assert(lv_obj_get_child_count(lv_screen_active()) == children_before);
        assert(timer_count() == timers_before);
    }
    if (mode == 1 || mode == 2) {
        for (unsigned i = 0; i < 9; ++i) assert(s[i] == 0);
        assert(!ui_voice_is_visible());
    } else if (mode == 3) {
        assert(s[0] == 1 && !s[1] && !s[2] && !s[3] && !s[4]);
        assert(!s[5] && !s[6] && !s[7] && !s[8]);
    } else if (mode == 4) {
        assert(s[0] == 1 && s[1] == 1 && s[2] == 1 && s[3] == 1 && s[4] == 1);
        assert(!s[5] && !s[6] && !s[7] && !s[8]);
    } else if (mode == 5) {
        assert(s[0] == 1 && s[1] == 1 && !s[2] && !s[3] && s[4] == 1);
        assert(s[5] == 1 && !s[6] && !s[7] && s[8] == 1);
    } else {
        assert(s[0] == 1 && s[1] == 1 && !s[2] && !s[3] && s[4] == 1);
        assert(s[5] == 1 && s[6] == 1 && s[7] == 1 && s[8] == 1);
    }
    std::printf("voice startup mode %d passed\n", mode);
}

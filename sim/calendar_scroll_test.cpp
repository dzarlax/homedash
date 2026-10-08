// Run the real calendar UI without SDL or a physical display.
#include "../src/ui_dashboard.cpp"
#include <cassert>

static int visible_rows()
{
    int count = 0;
    for (lv_obj_t *label : lbl_events) {
        if (label && !lv_obj_has_flag(label, LV_OBJ_FLAG_HIDDEN)) ++count;
    }
    return count;
}

int main()
{
    lv_init();
    lv_display_create(1024, 600);
    ui_dashboard_create();

    bridge_cal_data_t data = {};
    data.valid = true;
    // A fixed future date makes hero selection independent of the wall clock.
    data.year = 2099;
    data.month = 1;
    data.day = 15;
    data.count = 25;
    data.events.resize(data.count);
    for (int i = 0; i < data.count; ++i) {
        std::snprintf(data.events[i].summary, sizeof(data.events[i].summary), "Event %d", i);
        data.events[i].all_day = true;
    }
    data.events[24].all_day = false;
    data.events[24].end_hour = 23;
    data.events[24].end_min = 59;
    ui_dashboard_update_ha_calendar(&data);
    lv_obj_update_layout(lv_screen_active());
    assert(visible_rows() == 24);
    assert(std::strcmp(lv_label_get_text(lbl_hero_title), "Event 24") == 0);
    assert(std::strstr(lv_label_get_text(lbl_events[23]), "Event 23"));
    assert(lv_obj_get_scroll_dir(events_scroll) == LV_DIR_VER);
    assert(lv_obj_get_scrollbar_mode(events_scroll) == LV_SCROLLBAR_MODE_AUTO);
    assert(lv_obj_get_scroll_bottom(events_scroll) > 0);
    lv_obj_scroll_to_y(events_scroll, 40, LV_ANIM_OFF);
    const int scroll_y = lv_obj_get_scroll_y(events_scroll);
    assert(scroll_y > 0);
    ui_dashboard_update_ha_calendar(&data);
    lv_obj_update_layout(lv_screen_active());
    assert(lv_obj_get_scroll_y(events_scroll) == scroll_y);
    lv_obj_scroll_to_y(events_scroll, 10000, LV_ANIM_OFF);
    assert(lv_obj_get_scroll_bottom(events_scroll) == 0);

    data.count = 2;
    data.events.resize(2);
    ui_dashboard_update_ha_calendar(&data);
    lv_obj_update_layout(lv_screen_active());
    assert(visible_rows() == 2);

    data.count = 0;
    data.events.clear();
    ui_dashboard_update_ha_calendar(&data);
    assert(visible_rows() == 0);
    assert(!lv_obj_has_flag(lbl_no_events, LV_OBJ_FLAG_HIDDEN));
    assert(std::strcmp(lv_label_get_text(lbl_no_events), "Нет событий") == 0);
    data.valid = false;
    ui_dashboard_update_ha_calendar(&data);
    assert(visible_rows() == 0);
    std::puts("PASS: 25 events, late hero, scrolling, refresh, smaller/empty/error lists");
}

// Check the real LVGL layout, including clipping by the bottom navigation.
#include "../src/ui_dashboard.cpp"
#include <cassert>

extern void sim_set_connection_status(const char *data, const char *sync);
extern "C" void sim_init_fixtures(void);

int main()
{
    lv_init();
    lv_display_create(1024, 600);
    sim_init_fixtures();
    ui_dashboard_create();
    sim_set_connection_status("ESP_OK/404", "Sync:ESP_ERR_HTTP_CONNECT tls=8001/ffffd900/8 e=111");
    ui_dashboard_update_time();
    lv_obj_update_layout(lv_screen_active());
    assert(lv_obj_is_visible(lbl_connection_status));
    lv_area_t area;
    lv_obj_get_coords(lbl_connection_status, &area);
    assert(area.x1 >= 0 && area.x2 < 1024);
    assert(area.y1 >= 514 && area.y2 < 540);
    assert(lv_label_get_long_mode(lbl_connection_status) == LV_LABEL_LONG_DOT);
    const char *text = lv_label_get_text(lbl_connection_status);
    assert(!strcmp(text, ui_text("Нет связи с Bridge. Проверьте настройки")));
    assert(!strstr(text, "ESP_ERR") && !strstr(text, "404"));
    sim_set_connection_status("OK", "Sync:OK");
    ui_dashboard_update_time();
    assert(lv_obj_has_flag(lbl_connection_status, LV_OBJ_FLAG_HIDDEN));
    sim_set_connection_status("Fetching...", "Sync:OK");
    ui_dashboard_update_time();
    assert(lv_obj_has_flag(lbl_connection_status, LV_OBJ_FLAG_HIDDEN));
    sim_set_connection_status("ESP_OK/404", "Sync:waiting");
    ui_dashboard_update_time();
    assert(lv_obj_is_visible(lbl_connection_status));
    puts("PASS: connection errors remain above navigation; healthy status is hidden");
}

#define HOMEDASH_BRIGHTNESS_REFERENCE_TEST 1
#include "../src/ui_dashboard.cpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include "src/misc/lv_text_private.h"
#include <cstring>
static brightness_reference_status_t reference = {BRIGHTNESS_REFERENCE_IDLE, 0};
static brightness_comparison_t comparison = {};
static int reference_writes, cancellations, marks;
static uint64_t marked_id;
static bool marked_previous, marked_dark, marked_historical;
brightness_reference_status_t brightness_runtime_reference_status() { return reference; }
brightness_comparison_t brightness_runtime_comparison() { return comparison; }
bool brightness_runtime_reference_baseline() { ++reference_writes; reference.phase = BRIGHTNESS_REFERENCE_CONFIRM; comparison.armed=false; return true; }
bool brightness_runtime_reference_begin() { comparison.armed=true; comparison.valid=true;comparison.current=50;comparison.id++;return true; }
bool brightness_runtime_reference_step(uint64_t id, bool brighter, uint8_t step) {
    if (id != comparison.id || !comparison.armed) return false;
    reference_writes++;comparison.previous=comparison.current;comparison.current+=brighter?-step:step;
    comparison.has_previous=true;comparison.id++;reference={BRIGHTNESS_REFERENCE_PREVIEW,20,comparison.current};return true;
}
bool brightness_runtime_reference_mark(uint64_t id, bool previous, bool dark, bool historical) {
    marks++;marked_id=id;marked_previous=previous;marked_dark=dark;marked_historical=historical;return true;
}
bool brightness_runtime_reference_end() { cancellations++;comparison.armed=false;comparison.historical=comparison.valid;reference.phase=BRIGHTNESS_REFERENCE_RESTORED;return true; }
static void fits(lv_obj_t *label) {
    lv_point_t measured = {};
    lv_text_get_size(&measured, lv_label_get_text(label), lv_obj_get_style_text_font(label, 0),
        lv_obj_get_style_text_letter_space(label, 0), lv_obj_get_style_text_line_space(label, 0),
        lv_obj_get_width(label), LV_TEXT_FLAG_NONE);
    if (measured.y > lv_obj_get_height(label)) fprintf(stderr,"Clipped: %s (%d > %d)\n",lv_label_get_text(label),measured.y,(int)lv_obj_get_height(label));
    assert(measured.y <= lv_obj_get_height(label));
    uint32_t index=0;const char *text=lv_label_get_text(label);
    while(text[index]) {
        uint32_t cp=lv_text_encoded_next(text,&index);
        if(cp<=32)continue;
        lv_font_glyph_dsc_t glyph={};
        bool available=lv_font_get_glyph_dsc(lv_obj_get_style_text_font(label,0),&glyph,cp,0) && !glyph.is_placeholder;
        if(!available)fprintf(stderr,"Missing glyph U+%04x in %s\n",(unsigned)cp,text);
        assert(available);
    }
}
static void panel_fits(lv_obj_t *obj) {
    if(lv_obj_check_type(obj,&lv_label_class))fits(obj);
    for(uint32_t i=0;i<lv_obj_get_child_count(obj);i++)panel_fits(lv_obj_get_child(obj,i));
}
static void click(lv_obj_t *button) {
    assert(!lv_obj_has_state(button,LV_STATE_DISABLED));
    lv_obj_send_event(button,LV_EVENT_PRESSED,nullptr);lv_obj_send_event(button,LV_EVENT_CLICKED,nullptr);
}
static uint16_t pixels[1024*600], buffer[1024*600];
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *map) {
    auto src=(uint16_t *)map;
    for(int y=a->y1;y<=a->y2;y++) for(int x=a->x1;x<=a->x2;x++) pixels[y*1024+x]=*src++;
    lv_display_flush_ready(d);
}
static void capture(const char *locale,const char *state) {
    const char *dir=getenv("BRIGHTNESS_QA_DIR");if(!dir)return;
    lv_refr_now(lv_display_get_default());
    char path[512];snprintf(path,sizeof(path),"%s/%s-%s.ppm",dir,locale,state);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n1024 600\n255\n");
    for(auto p:pixels){unsigned char rgb[3]={(unsigned char)(((p>>11)&31)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};fwrite(rgb,1,3,f);}fclose(f);
}
int main() {
    lv_init(); auto display=lv_display_create(1024, 600);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,nullptr,sizeof(buffer),LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display,flush); ui_dashboard_create(); ui_dashboard_open_settings();
    assert(reference_writes==0 && lv_obj_has_flag(settings_reference_panel,LV_OBJ_FLAG_HIDDEN));
    reference_open_cb(nullptr); assert(reference_writes==0);
    reference_test_cb(nullptr);assert(reference_writes==1);
    reference_test_cb(nullptr);assert(reference_writes==1 && comparison.armed);
    int before_hold=reference_writes;
    lv_obj_send_event(settings_step_buttons[0],LV_EVENT_PRESSED,nullptr);
    for(int i=0;i<5;i++)lv_obj_send_event(settings_step_buttons[0],LV_EVENT_LONG_PRESSED_REPEAT,nullptr);
    assert(reference_writes==before_hold);
    lv_obj_send_event(settings_step_buttons[0],LV_EVENT_CLICKED,nullptr);
    assert(reference_writes==before_hold+1);
    click(settings_step_buttons[0]);click(settings_step_buttons[0]);
    assert(comparison.previous==40 && comparison.current==35);
    click(settings_mark_buttons[0]);assert(marked_previous && !marked_dark && marked_id==comparison.id);
    click(settings_mark_buttons[1]);assert(!marked_previous);
    // Press captures what was actually shown; a refresh before release cannot silently pick the new pair.
    lv_obj_send_event(settings_mark_buttons[0],LV_EVENT_PRESSED,nullptr);
    uint64_t pressed_id=comparison.id; comparison.id++;comparison.current=30;refresh_reference_test();
    lv_obj_send_event(settings_mark_buttons[0],LV_EVENT_CLICKED,nullptr);assert(marked_id==pressed_id);
    reference_target_cb(nullptr);reference_step_size_cb(nullptr);assert(settings_step_size==1);
    comparison.historical=true;comparison.armed=false;reference.phase=BRIGHTNESS_REFERENCE_RESTORED;refresh_reference_test();
    click(settings_mark_buttons[0]);assert(marked_dark && marked_historical);
    for (const char *locale : {"ru","en","sr-Latn"}) {
        ui_strings_set_locale(locale);refresh_static_ui_labels();
        for (auto phase : {BRIGHTNESS_REFERENCE_IDLE,BRIGHTNESS_REFERENCE_CONFIRM,BRIGHTNESS_REFERENCE_PREVIEW,
                           BRIGHTNESS_REFERENCE_RESTORED,BRIGHTNESS_REFERENCE_ERROR,BRIGHTNESS_REFERENCE_DISABLED}) {
            reference={phase,12,75};comparison.current=75;comparison.previous=70;
            comparison.has_previous=true;comparison.valid=phase!=BRIGHTNESS_REFERENCE_ERROR && phase!=BRIGHTNESS_REFERENCE_IDLE && phase!=BRIGHTNESS_REFERENCE_DISABLED;
            comparison.armed=phase==BRIGHTNESS_REFERENCE_PREVIEW;
            comparison.historical=phase==BRIGHTNESS_REFERENCE_RESTORED;
            refresh_reference_test();lv_obj_update_layout(lv_screen_active());
            for (auto label : {settings_reference_status,settings_reference_label,settings_comparison_label,
                settings_result_label,settings_step_size_label,settings_target_label,settings_mark_labels[0],settings_mark_labels[1],settings_step_labels[0],settings_step_labels[1]}) fits(label);
            panel_fits(settings_reference_panel);
            if(phase==BRIGHTNESS_REFERENCE_PREVIEW) capture(locale,"preview");
            if(phase==BRIGHTNESS_REFERENCE_RESTORED) capture(locale,"history");
            assert(!lv_obj_has_state(settings_step_buttons[0],LV_STATE_DISABLED)==comparison.armed);
            if (comparison.valid) assert(strstr(lv_label_get_text(settings_mark_labels[0]),"70"));
            assert(lv_obj_get_y(settings_mark_buttons[0])+lv_obj_get_height(settings_mark_buttons[0])<lv_obj_get_y(settings_mark_buttons[1]));
        }
    }
    for(const char *locale : {"ru","en","sr-Latn"}) {
        ui_strings_set_locale(locale);refresh_static_ui_labels();
        comparison.valid=true;comparison.has_previous=true;comparison.armed=true;comparison.historical=false;
        comparison.previous=40;comparison.current=35;comparison.has_bright=true;comparison.bright=40;
        settings_dark_target=false;settings_step_size=5;reference={BRIGHTNESS_REFERENCE_PREVIEW,18,35};
        refresh_reference_test();lv_obj_update_layout(lv_screen_active());capture(locale,"bright-previous");
        assert(strstr(lv_label_get_text(settings_mark_labels[0]),"40"));
    }
    comparison.valid=true;comparison.armed=true;comparison.historical=false;reference.phase=BRIGHTNESS_REFERENCE_PREVIEW;
    reference_open_cb(nullptr);int old=cancellations;ui_dashboard_sim_set_page(0);settings_privacy_tick(nullptr);
    assert(cancellations==old+1 && !comparison.armed && lv_obj_has_flag(settings_reference_panel,LV_OBJ_FLAG_HIDDEN));
    ui_dashboard_open_settings();reference_open_cb(nullptr);comparison.armed=true;old=cancellations;set_screen_off(true);
    assert(cancellations==old+1);set_screen_off(false);
    reference_open_cb(nullptr);comparison.armed=true;old=cancellations;ui_dashboard_cancel_brightness_test();assert(cancellations==old+1);
    puts("brightness comparison UI: explicit previous/current IDs, captured taps, history, cancellation and RU/EN/sr-Latn fit passed");
}

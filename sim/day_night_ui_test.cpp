#include "../src/ui_dashboard.cpp"
#include "brightness_fixture.h"
#include <cassert>
#include <cstdio>
#include <string>
static uint16_t pixels[1024*600],buffer[1024*600];
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *map) {
 auto src=(uint16_t *)map;for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++)pixels[y*1024+x]=*src++;lv_display_flush_ready(d);
}
static void capture(const char *locale) {
 const char *dir=getenv("BRIGHTNESS_QA_DIR");if(!dir)return;lv_refr_now(lv_display_get_default());
 char path[512];snprintf(path,sizeof(path),"%s/display-preview-%s.ppm",dir,locale);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n1024 600\n255\n");
 for(auto p:pixels){unsigned char rgb[3]={(unsigned char)(((p>>11)&31)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};fwrite(rgb,1,3,f);}fclose(f);
}
int main() {
 lv_init();auto display=lv_display_create(1024,600);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(display,buffer,nullptr,sizeof(buffer),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(display,flush);ui_dashboard_create();
 // Production Settings expose Bridge/recovery, never the retired on-device calibration.
 assert(!settings_reference_panel && !settings_reference_button);
 namespace f=brightness_fixture;
 web_preferences_t p={};p.backlight_percent=100;p.night_backlight_percent=36;p.night_enabled=true;p.night_dim=true;p.idle_photos=true;p.photo_idle_seconds=3600;
 strcpy(p.night_start,"23:00");strcpy(p.night_end,"07:00");
 struct tm t={};t.tm_hour=22;t.tm_min=59;assert(!clock_is_night_for(p,t));t.tm_hour=23;t.tm_min=0;assert(clock_is_night_for(p,t));
 t.tm_hour=6;t.tm_min=59;assert(clock_is_night_for(p,t));t.tm_hour=7;t.tm_min=0;assert(!clock_is_night_for(p,t));
 // DST fall-back repeats 02:30; both occurrences remain in the same night window.
 const char *tz=getenv("TZ");std::string old_tz=tz?tz:"";bool had_tz=tz;
 setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1);tzset();
 struct tm utc={};utc.tm_year=126;utc.tm_mon=9;utc.tm_mday=25;utc.tm_min=30;
 time_t first=timegm(&utc),second=first+3600;struct tm a={},b={};localtime_r(&first,&a);localtime_r(&second,&b);
 assert(a.tm_hour==2 && b.tm_hour==2 && a.tm_isdst!=b.tm_isdst && clock_is_night_for(p,a) && clock_is_night_for(p,b));
 if(had_tz)setenv("TZ",old_tz.c_str(),1);else unsetenv("TZ");tzset();
 f::ready=true;f::time_valid=false;int before=f::schedule_calls;
 assert(!ui_dashboard_apply_brightness(&p) && f::schedule_calls==before);
 p.night_enabled=false;assert(ui_dashboard_apply_brightness(&p) && !f::is_night && f::day==100 && f::night==36);
 f::time_valid=true;p.night_enabled=true;strcpy(p.night_start,"00:00");strcpy(p.night_end,"00:00");
 wake_override_until=0;assert(ui_dashboard_apply_brightness(&p) && f::is_night && !f::wake);
 wake_override_until=esp_timer_get_time()/1000000+60;assert(ui_dashboard_apply_brightness(&p) && f::wake);wake_override_until=0;
 p.night_dim=false;ui_preferences=p;ui_has_preferences=true;ui_dashboard_update_time();assert(ui_screen_off);
 f::preview=true;ui_dashboard_show_backlight_preview();assert(!ui_screen_off && backlight_preview_bar && !lv_obj_has_flag(backlight_preview_bar,LV_OBJ_FLAG_HIDDEN));ui_dashboard_update_time();assert(!ui_screen_off);
 for(const char *locale : {"ru","en","sr-Latn"}) {
 ui_strings_set_locale(locale);refresh_static_ui_labels();refresh_backlight_preview();lv_obj_update_layout(lv_screen_active());lv_obj_update_layout(lv_layer_top());
 lv_point_t size={};lv_text_get_size(&size,lv_label_get_text(backlight_preview_text),lv_obj_get_style_text_font(backlight_preview_text,0),0,0,lv_obj_get_width(backlight_preview_text),LV_TEXT_FLAG_NONE);assert(size.y<=lv_obj_get_height(backlight_preview_text));capture(locale);
 }
 f::preview=false;ui_dashboard_update_time();refresh_backlight_preview();assert(ui_screen_off && lv_obj_has_flag(backlight_preview_bar,LV_OBJ_FLAG_HIDDEN));
 p.backlight_percent=0;before=f::clear_calls;assert(ui_dashboard_apply_brightness(&p) && f::clear_calls==before+1);
 f::ready=false;f::time_valid=true;
 puts("day/night UI: midnight, DST, clock gating, wake override, off-mode preview and clear path passed");
}

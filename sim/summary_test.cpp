#include "../src/ui_dashboard.cpp"
#include <cassert>
#include <cstdio>
static uint16_t pixels[1024*600],buffer[1024*600];
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *map){auto src=(uint16_t*)map;for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++)pixels[y*1024+x]=*src++;lv_display_flush_ready(d);}
static void shot(const char *name){const char *dir=getenv("SUMMARY_QA_DIR");if(!dir)return;lv_refr_now(lv_display_get_default());char path[512];snprintf(path,sizeof(path),"%s/%s.ppm",dir,name);FILE*f=fopen(path,"wb");assert(f);fprintf(f,"P6\n1024 600\n255\n");for(auto p:pixels){unsigned char rgb[3]={(unsigned char)(((p>>11)&31)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};fwrite(rgb,1,3,f);}fclose(f);}
int main(){
 lv_init();auto display=lv_display_create(1024,600);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(display,buffer,nullptr,sizeof(buffer),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(display,flush);ui_dashboard_create();lv_tileview_set_tile(tileview,nav_tiles[1],LV_ANIM_OFF);
 bridge_data_t data={};ui_dashboard_update_bridge(&data);assert(!strcmp(lv_label_get_text(lbl_no_tasks),ui_text("Загружаю данные…")));
 data.health.valid=data.tasks_valid=data.news_valid=true;data.health.presence_known=true;data.health.present=0x7f;data.health.sleep=7.7;data.health.steps=4800;data.health.hr=72;data.health.rhr=58;data.health.hrv=45;data.health.spo2=98;data.health.readiness=80;
 data.health_state=data.tasks_state=data.news_state={true,true,false,true,true,120,0};
 data.task_count=8;data.news_count=5;
 for(int i=0;i<8;i++){snprintf(data.tasks[i].title,sizeof(data.tasks[i].title),"Example task %d — long readable text",i+1);snprintf(data.tasks[i].due,sizeof(data.tasks[i].due),"2026-11-01");data.tasks[i].priority=3;}
 for(int i=0;i<5;i++){snprintf(data.news[i].title,sizeof(data.news[i].title),"Example news %d with enough text to wrap on two lines",i+1);snprintf(data.news[i].category,sizeof(data.news[i].category),"Local");data.news[i].hours_ago=2;}
 for(const char *locale:{"ru","en","sr-Latn"}) {
  ui_strings_set_locale(locale);refresh_static_ui_labels();ui_dashboard_update_bridge(&data);lv_obj_update_layout(lv_screen_active());
  for(int i=0;i<6;i++)assert(strcmp(lv_label_get_text(metric_cards[i].lbl_value),"—"));
  assert(lv_obj_get_scroll_bottom(summary_tasks_body)>0&&lv_obj_get_scroll_bottom(summary_news_body)>0);
  assert(lv_obj_get_y(page2_news_panel)+lv_obj_get_height(page2_news_panel)<=524);
  char file[48];snprintf(file,sizeof(file),"summary-%s",locale);shot(file);
 }
 data.news_state.source_age_seconds=7200;ui_dashboard_update_bridge(&data);assert(strstr(lv_label_get_text(lbl_news_age[0]),"4"));data.news_state.source_age_seconds=120;
 lv_obj_send_event(summary_task_rows[0],LV_EVENT_CLICKED,nullptr);assert(reader_overlay);reader_close_cb(nullptr);
 lv_obj_send_event(summary_news_rows[0],LV_EVENT_CLICKED,nullptr);assert(reader_overlay);reader_close_cb(nullptr);
 data.health.present&=~(1u<<2);ui_dashboard_update_bridge(&data);assert(!strcmp(lv_label_get_text(metric_cards[2].lbl_value),"—"));assert(strstr(lv_label_get_text(metric_cards[1].lbl_value),"4800"));
 data.health_state.last_attempt_failed=data.tasks_state.last_attempt_failed=data.news_state.last_attempt_failed=true;ui_dashboard_update_bridge(&data);assert(!lv_obj_has_flag(lbl_task_lines[0],LV_OBJ_FLAG_HIDDEN));assert(strstr(lv_label_get_text(summary_tasks_status),"2"));shot("summary-stale");
 data.tasks_valid=data.news_valid=false;ui_dashboard_update_bridge(&data);assert(!strcmp(lv_label_get_text(lbl_no_tasks),ui_text("Не удалось загрузить данные")));
 data.tasks_valid=data.news_valid=true;data.task_count=data.news_count=0;data.tasks_state.last_attempt_failed=data.news_state.last_attempt_failed=false;ui_dashboard_update_bridge(&data);assert(!strcmp(lv_label_get_text(lbl_no_tasks),ui_text("Нет задач")));assert(!strcmp(lv_label_get_text(lbl_no_news),ui_text("Нет новостей")));shot("summary-empty");
 web_preferences_t p={};web_preferences_defaults(&p);p.revision=1;snprintf(p.settings_url,sizeof(p.settings_url),"https://example.test/settings/");p.visible_health=false;p.visible_tasks=false;p.visible_news=true;ui_dashboard_apply_preferences(&p);lv_obj_update_layout(lv_screen_active());assert(lv_obj_get_width(page2_news_panel)==996&&lv_obj_get_y(page2_news_panel)==8);shot("summary-news-only");
 p.visible_news=false;ui_dashboard_apply_preferences(&p);assert(!lv_obj_has_flag(summary_empty,LV_OBJ_FLAG_HIDDEN));
 // The returned fresh result clears an earlier calendar warning for the same date.
 bridge_cal_data_t cal={};cal.valid=true;cal.sources_partial=cal.cache_stale=cal.completeness_known=true;cal.year=sel_year;cal.month=sel_month;cal.day=sel_day;ui_dashboard_update_ha_calendar(&cal);char expected[192];snprintf(expected,sizeof(expected),ui_text("Ожидает обновления · получено %d мин назад"),0);assert(!strcmp(lv_label_get_text(lbl_calendar_freshness),expected));
 cal.sources_partial=cal.cache_stale=false;ui_dashboard_update_ha_calendar(&cal);assert(!strcmp(lv_label_get_text(lbl_calendar_freshness),ui_text("Получено дисплеем только что")));
 puts("PASS: Summary loading/empty/missing/stale, all metrics, scrolling, locale rendering, disabled reflow and calendar recovery");
}

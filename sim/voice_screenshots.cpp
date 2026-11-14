#include "../src/ui_voice.h"
#include "../src/ui_strings.h"
#include <cstdio>
#include <string>
#include <vector>
static uint16_t pixels[1024*600], drawbuf[1024*40];
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *map) {
    auto *src = reinterpret_cast<uint16_t *>(map);
    for (int y=a->y1; y<=a->y2; ++y) for (int x=a->x1; x<=a->x2; ++x) pixels[y*1024+x]=*src++;
    lv_display_flush_ready(d);
}
static void shot(const char *dir, const char *name, ui_voice_state_t state) {
    ui_voice_show(&state); lv_tick_inc(200); lv_timer_handler(); lv_obj_invalidate(lv_screen_active()); lv_refr_now(nullptr);
    std::string path=std::string(dir)+"/"+name+".ppm"; FILE *f=fopen(path.c_str(),"wb"); if(!f) return;
    fprintf(f,"P6\n1024 600\n255\n");
    for (auto c:pixels) { unsigned char rgb[3]={(unsigned char)(((c>>11)&31)*255/31),(unsigned char)(((c>>5)&63)*255/63),(unsigned char)((c&31)*255/31)}; fwrite(rgb,1,3,f); } fclose(f);
}
int main(int argc,char **argv) {
    if(argc!=2) return 1; lv_init(); auto *d=lv_display_create(1024,600);
    lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565); lv_display_set_buffers(d,drawbuf,nullptr,sizeof(drawbuf),LV_DISPLAY_RENDER_MODE_PARTIAL); lv_display_set_flush_cb(d,flush);
    ui_strings_set_locale("ru"); ui_voice_create(lv_screen_active(),nullptr,nullptr,nullptr);
    shot(argv[1],"listening",{UI_VOICE_LISTENING,"",false,false});
    shot(argv[1],"lookup",{UI_VOICE_LOOKUP,"Сейчас посмотрю информацию.",false,false});
    shot(argv[1],"answer",{UI_VOICE_ANSWER,"Сегодня в Белграде солнечно. Температура около двадцати градусов.\n\nХорошего дня!",false,false});
    shot(argv[1],"interrupted",{UI_VOICE_ANSWER,"Первая часть.\n— Прервано —\nПоздний фрагмент продолжается здесь.",true,false});
    std::string text; for(int i=0;i<80;++i) text+="Длинный ответ с переносами и кириллицей. ";
    shot(argv[1],"long",{UI_VOICE_ANSWER,text.c_str(),false,true});
    shot(argv[1],"offline",{UI_VOICE_OFFLINE,"",false,false});
}

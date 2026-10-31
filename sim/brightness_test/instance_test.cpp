#include "brightness_runtime.h"
#include "io_extension.h"
#include "runtime_freertos.h"
#include <cassert>
#include <cstdio>
#include <cstring>
static int64_t now_us;
static int writes,last=-1,failures;
int64_t esp_timer_get_time(){return now_us;}
esp_err_t IO_EXTENSION_Pwm_Try_Output(uint8_t command){++writes;if(failures){--failures;return 9;}last=command;return ESP_OK;}
int main(){
 const char *id="6a36f507bf145264d1be1479864ad342";
 assert(!brightness_instance_profile_for("other"));
 auto p=brightness_instance_profile_for(id);assert(p);
 assert(!brightness_runtime_apply_instance(p,id) && writes==0);
 assert(brightness_runtime_start());
 assert(!brightness_runtime_apply_instance(p,"other") && writes==0);
 auto invalid=*p;invalid.dim_command=98;assert(!brightness_runtime_apply_instance(&invalid,id) && writes==0);
 failures=1;assert(!brightness_runtime_apply_instance(p,id));
 assert(!brightness_runtime_ready() && !brightness_runtime_report().output_known);
 assert(brightness_runtime_apply_instance(p,id) && last==50 && brightness_runtime_ready());
 assert(!brightness_runtime_settings_confirmed());
 assert(brightness_runtime_schedule(100,36,false,false) && last==1 && brightness_runtime_settings_confirmed());
 assert(brightness_runtime_schedule(100,36,true,false) && last==70);
 assert(brightness_runtime_schedule(100,10,true,false) && last==97);
 assert(brightness_runtime_schedule(100,36,true,true) && last==1);
 assert(brightness_runtime_adjust(36) && last==70);
 auto r=brightness_runtime_report();assert(r.test_command==70 && r.return_command==1 && r.restore_deadline_uptime_ms==20000);
 now_us=15000000;assert(brightness_runtime_adjust(10) && last==97);
 r=brightness_runtime_report();assert(r.return_command==1 && r.restore_deadline_uptime_ms==35000);
 assert(brightness_runtime_schedule(100,36,true,false) && last==97); // Preview wins.
 assert(!brightness_runtime_clear_schedule());
 now_us=35000000;run_restore_worker_step();assert(last==1 && !brightness_runtime_preview_active());
 assert(brightness_runtime_clear_schedule() && last==50);
 assert(!brightness_runtime_adjust(100)); // Expired continuation cannot silently restart.
 assert(brightness_runtime_restore());
 assert(brightness_runtime_adjust(100) && last==1);
 for(int i=0;i<7;i++){now_us+=15000000;assert(brightness_runtime_adjust(i%2?100:36));}
 assert(brightness_runtime_report().restore_deadline_uptime_ms==155000);
 now_us=155000000;run_restore_worker_step();assert(last==50);
 assert(!brightness_runtime_adjust(100));assert(brightness_runtime_restore());
 assert(brightness_runtime_adjust(100));failures=2;
 assert(!brightness_runtime_adjust(36));assert(!brightness_runtime_ready() && !brightness_runtime_settings_confirmed());
 run_restore_worker_step();assert(last==50 && brightness_runtime_ready());
 assert(brightness_runtime_preview(36));assert(!brightness_runtime_adjust(100)); // Never take over legacy preview.
 assert(brightness_runtime_restore());
 puts("verified instance: binding, ACK, normalized mapping, wake, live session, original return and recovery passed");
}

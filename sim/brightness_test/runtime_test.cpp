#include "brightness_runtime.h"
#include "io_extension.h"
#include <cassert>
#include <cstdio>
#include <cstring>
static int64_t now_us; static int writes, last, failure;
int64_t esp_timer_get_time() { return now_us; }
esp_err_t IO_EXTENSION_Pwm_Try_Output(uint8_t value) { ++writes; last=value; return failure ? 9 : ESP_OK; }
int main() {
    assert(!brightness_runtime_ready()); assert(!brightness_runtime_preview(50)); assert(writes==0);
    assert(!brightness_runtime_configure_verified(50,50,10));
    assert(brightness_runtime_configure_verified(90,10,50)); assert(brightness_runtime_ready());
    assert(brightness_runtime_preview(100) && last==10); now_us=19999000; brightness_runtime_tick(); assert(last==10);
    now_us=20000000; failure=1; brightness_runtime_tick(); assert(last==50);
    failure=0; brightness_runtime_tick(); assert(last==50);
    assert(brightness_runtime_restore());
    brightness_checkpoint_t checkpoint = {}; assert(brightness_runtime_checkpoint(&checkpoint));
    assert(brightness_runtime_schedule(100,10,true,false) && last==90);
    assert(brightness_runtime_rollback(&checkpoint) && last==50);
    failure=1; assert(!brightness_runtime_rollback(&checkpoint));
    assert(!brightness_runtime_ready() && !strcmp(brightness_runtime_error(), "write_failed"));
    puts("brightness runtime: disabled by default and monotonic restore retry passed");
}

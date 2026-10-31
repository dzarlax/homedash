#include "brightness_runtime.h"
#include "io_extension.h"
#include "runtime_freertos.h"
#include <atomic>
#include <cassert>
#include <thread>
#include <vector>
#include <cstdio>
static std::atomic<int64_t> now_us{0};
static std::atomic<int> writes{0}, in_write{0}, last{0};
int64_t esp_timer_get_time() { return now_us.load(); }
esp_err_t IO_EXTENSION_Pwm_Try_Output(uint8_t value) {
    assert(in_write.fetch_add(1)==0);last=value;++writes;assert(in_write.fetch_sub(1)==1);return ESP_OK;
}
int main() {
    assert(brightness_runtime_start() && brightness_runtime_reference_baseline() && brightness_runtime_reference_begin());
    auto id=brightness_runtime_comparison().id;std::atomic<int> accepted{0};std::vector<std::thread> threads;
    for(int i=0;i<16;i++)threads.emplace_back([&]{if(brightness_runtime_reference_step(id,true,5))++accepted;});
    for(auto &t:threads)t.join();assert(accepted==1 && writes==2 && last==45);
    auto pair=brightness_runtime_comparison();assert(pair.previous==50 && pair.current==45 && pair.id!=id);
    now_us=20000000;
    std::thread worker([]{run_restore_worker_step();});
    std::thread marker([&]{assert(!brightness_runtime_reference_mark(pair.id,true,false,false));});
    std::thread reader([]{for(int i=0;i<100;i++){auto c=brightness_runtime_comparison();assert(c.current==45 && c.previous==50);auto r=brightness_runtime_report();assert(r.output_known && (r.last_command==45 || r.last_command==50));}});
    worker.join();marker.join();reader.join();assert(last==50 && writes==3);
    assert(brightness_runtime_reference_mark(pair.id,true,false,true));
    puts("brightness comparison concurrency: one step per snapshot, serialized deadline/selection, coherent telemetry passed");
}

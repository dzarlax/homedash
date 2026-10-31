#include "brightness_runtime.h"
#include "io_extension.h"
#include "runtime_freertos.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

static std::atomic<int64_t> now_us{0};
static std::atomic<int> writes{0}, in_write{0}, last{0};
int64_t esp_timer_get_time() { return now_us.load(); }
esp_err_t IO_EXTENSION_Pwm_Try_Output(uint8_t value) {
    assert(in_write.fetch_add(1) == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    last = value;
    ++writes;
    assert(in_write.fetch_sub(1) == 1);
    return ESP_OK;
}
int main() {
    assert(brightness_runtime_start());
    const brightness_board_identity_t board = {"test-board", "fixture-only"};
    const brightness_calibration_profile_t profile = {
        1, 1, "test-board", "fixture-only", 0, 97, 90, 10, 50, true, "synthetic host fixture"
    };
    assert(brightness_runtime_apply_profile(&profile, &board) && writes == 0);
    std::atomic<int> accepted{0};
    std::vector<std::thread> contenders;
    for (int i = 0; i < 16; ++i) contenders.emplace_back([&] {
        if (brightness_runtime_preview(70)) ++accepted;
        assert(brightness_runtime_ready());
    });
    for (auto &thread : contenders) thread.join();
    assert(accepted == 1 && writes == 1);
    brightness_checkpoint_t snapshot = {};
    assert(brightness_runtime_checkpoint(&snapshot) && snapshot.restore == 50 && snapshot.deadline_ms == 20000);
    now_us = 20000000;
    std::thread worker([] { run_restore_worker_step(); });
    std::thread manual([] { assert(brightness_runtime_restore()); });
    std::thread observer([] {
        for (int i = 0; i < 100; ++i) {
            brightness_checkpoint_t current = {};
            assert(brightness_runtime_checkpoint(&current));
            assert(current.current <= 97);
            assert(!*brightness_runtime_error());
            auto report = brightness_runtime_report();
            assert(report.output_known && report.has_last_command && report.has_test);
            assert(report.last_command == report.test_command || report.last_command == report.return_command);
        }
    });
    worker.join(); manual.join(); observer.join();
    assert(writes == 2 && last == 50 && in_write == 0);
    assert(brightness_runtime_checkpoint(&snapshot) && !snapshot.preview);
    puts("brightness concurrency: one accepted preview, serialized writes, concurrent timeout/manual restore and telemetry passed");
}

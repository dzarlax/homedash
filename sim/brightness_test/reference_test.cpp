#include "brightness_runtime.h"
#include "io_extension.h"
#include "runtime_freertos.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
static int64_t now_us;
static int writes, last = -1, failures;
int64_t esp_timer_get_time() { return now_us; }
esp_err_t IO_EXTENSION_Pwm_Try_Output(uint8_t command) {
    ++writes;
    assert(command == 40 || command == 50 || command == 60 || command == 70);
    if (failures) { --failures; return 9; }
    last = command; return ESP_OK;
}
int main() {
    assert(!brightness_runtime_reference_baseline() && writes == 0);
    assert(brightness_runtime_start());
    auto report = brightness_runtime_report();
    assert(!strcmp(report.phase, "unknown") && !report.has_last_command && !report.output_known);
    run_restore_worker_step(); assert(writes == 0 && !brightness_runtime_ready());
    assert(brightness_runtime_reference_status().phase == BRIGHTNESS_REFERENCE_IDLE);
    assert(!brightness_runtime_reference_preview() && writes == 0);
    assert(brightness_runtime_reference_baseline() && last == 50);
    assert(brightness_runtime_reference_status().phase == BRIGHTNESS_REFERENCE_CONFIRM);
    report = brightness_runtime_report();
    assert(!strcmp(report.phase, "reference") && report.output_known && report.last_command == 50);
    now_us = 60000000; run_restore_worker_step(); assert(writes == 1);
    assert(!brightness_runtime_reference_baseline() && writes == 1);
    assert(brightness_runtime_reference_preview() && last == 60);
    assert(brightness_runtime_reference_status().remaining_seconds == 20);
    report = brightness_runtime_report();
    assert(!strcmp(report.phase, "preview") && report.output_known && report.last_command == 60);
    assert(report.has_test && report.test_command == 60 && report.return_command == 50);
    assert(report.sample_uptime_ms == 60000 && report.restore_deadline_uptime_ms == 80000);
    assert(!brightness_runtime_reference_preview() && writes == 2);
    assert(!brightness_runtime_reference_baseline() && writes == 2);
    assert(!brightness_runtime_preview(70) && !brightness_runtime_schedule(50, 50, false, false));
    now_us += 19999000; run_restore_worker_step(); assert(last == 60);
    assert(brightness_runtime_reference_status().remaining_seconds == 1);
    now_us += 1000; failures = 1; run_restore_worker_step();
    assert(brightness_runtime_reference_status().phase == BRIGHTNESS_REFERENCE_ERROR);
    report = brightness_runtime_report();
    assert(!strcmp(report.phase, "error") && !report.output_known && report.last_command == 60 && !strcmp(report.error, "write_failed"));
    assert(!brightness_runtime_reference_preview() && !brightness_runtime_reference_baseline());
    run_restore_worker_step(); assert(last == 50 && writes == 4);
    assert(brightness_runtime_reference_status().phase == BRIGHTNESS_REFERENCE_RESTORED);
    report = brightness_runtime_report();
    assert(!strcmp(report.phase, "restored") && report.output_known && report.last_command == 50 && report.test_command == 60);
    assert(!brightness_runtime_ready()); // Diagnostic is never a calibrated profile.
    assert(brightness_runtime_reference_baseline() && brightness_runtime_reference_preview());
    assert(brightness_runtime_restore() && last == 50);
    assert(brightness_runtime_reference_baseline());
    failures = 2;
    assert(!brightness_runtime_reference_preview()); // Failed preview and failed rollback.
    assert(brightness_runtime_reference_status().phase == BRIGHTNESS_REFERENCE_ERROR);
    assert(brightness_runtime_restore() && last == 50);
    failures = 1;
    assert(!brightness_runtime_reference_baseline()); // Failed initial reference write.
    assert(brightness_runtime_reference_status().phase == BRIGHTNESS_REFERENCE_ERROR);
    run_restore_worker_step(); assert(last == 50);
    for (uint8_t command : {40, 70}) {
        int count = writes;
        assert(!brightness_runtime_reference_preview(42) && writes == count);
        assert(brightness_runtime_reference_preview(command) && last == command);
        assert(brightness_runtime_reference_status().test_command == command);
        report = brightness_runtime_report();
        assert(report.output_known && report.last_command == command && report.test_command == command && report.return_command == 50);
        count = writes;
        assert(!brightness_runtime_reference_preview(command) && writes == count);
        assert(!brightness_runtime_reference_preview(command == 40 ? 70 : 40) && writes == count);
        now_us += 20000000;
        run_restore_worker_step();
        assert(last == 50 && brightness_runtime_reference_status().phase == BRIGHTNESS_REFERENCE_RESTORED);
    }
    puts("brightness reference: explicit baseline, readability gate, 50/60/50 and 40/70 candidates, independent deadline, manual restore and fault recovery passed");
}

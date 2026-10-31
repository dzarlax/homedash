#include "brightness_runtime.h"
#include "io_extension.h"
#include "runtime_freertos.h"
#include <cassert>
#include <cstdio>
#include <cstring>
static int64_t now_us;
static int writes, last = -1, failures;
int64_t esp_timer_get_time() { return now_us; }
esp_err_t IO_EXTENSION_Pwm_Try_Output(uint8_t command) {
    ++writes; assert(command >= 1 && command <= 97);
    if (failures) { --failures; return 9; }
    last = command; return ESP_OK;
}
static auto snap() { return brightness_runtime_comparison(); }
static void step(bool brighter, int amount = 5) {
    assert(brightness_runtime_reference_step(snap().id, brighter, amount));
}
static void begin() {
    assert(brightness_runtime_reference_baseline());
    assert(brightness_runtime_reference_begin());
    assert(last == 50 && snap().armed && !snap().has_previous);
}
int main() {
    assert(!brightness_runtime_reference_baseline() && writes == 0);
    assert(brightness_runtime_start()); run_restore_worker_step();
    assert(!brightness_runtime_report().output_known && writes == 0);
    assert(!brightness_runtime_reference_step(0, true, 5));
    begin(); int before = writes;
    assert(!brightness_runtime_reference_mark(snap().id, true, false, false));
    assert(!brightness_runtime_reference_step(snap().id, true, 2) && writes == before);
    step(true); step(true); auto at40 = snap(); step(true); auto pair = snap();
    assert(pair.previous == 40 && pair.current == 35);
    assert(!brightness_runtime_reference_mark(at40.id, true, false, false));
    assert(brightness_runtime_reference_mark(pair.id, true, false, false));
    assert(snap().bright == 40 && last == 35); // Selection is not current output.
    auto report = brightness_runtime_report();
    assert(!strcmp(report.phase, "preview") && report.last_command == 35 && report.return_command == 50);
    now_us = 20000000;
    // An active button pressed before expiry cannot become a historical selection.
    assert(!brightness_runtime_reference_mark(pair.id, false, false, false));
    assert(last == 50 && snap().historical && !snap().armed);
    assert(brightness_runtime_reference_mark(pair.id, true, false, true));
    assert(!brightness_runtime_reference_step(pair.id, true, 5));
    begin(); assert(!brightness_runtime_reference_mark(pair.id, true, false, true));
    for (int i=0; i<5; ++i) step(false);
    pair = snap(); assert(pair.previous == 70 && pair.current == 75);
    assert(brightness_runtime_reference_mark(pair.id, true, true, false) && snap().dark == 70);
    assert(brightness_runtime_reference_mark(pair.id, false, true, false) && snap().dark == 75);
    assert(!brightness_runtime_reference_mark(pair.id, false, false, false)); // Invalid endpoint order.
    step(true, 1); assert(last == 74);
    assert(brightness_runtime_reference_end() && last == 50);
    before = writes; assert(brightness_runtime_reference_end() && writes == before);
    begin();
    while (snap().current > 1) step(true);
    assert(last == 1); before = writes;
    auto deadline = brightness_runtime_report().restore_deadline_uptime_ms;
    assert(!brightness_runtime_reference_step(snap().id, true, 5) && writes == before);
    assert(brightness_runtime_report().restore_deadline_uptime_ms == deadline);
    while (snap().current < 97) step(false);
    assert(last == 97); before = writes;
    assert(!brightness_runtime_reference_step(snap().id, false, 1) && writes == before);
    assert(brightness_runtime_reference_end());
    // The 20-second inactivity deadline slides, the two-minute session limit does not.
    begin(); uint64_t start = now_us / 1000; step(true);
    for (int i=1; i<=7; ++i) { now_us += 15000000; step(i % 2 == 0); }
    assert(brightness_runtime_report().restore_deadline_uptime_ms == start + 120000);
    now_us += 15000000;
    assert(!brightness_runtime_reference_step(snap().id, true, 5) && last == 50);
    // Failed step never replaces a comparison and invalidates old selection tokens.
    begin(); step(true); auto old = snap(); failures = 1;
    assert(!brightness_runtime_reference_step(old.id, true, 5));
    assert(last == 50 && !snap().valid && snap().step_failed);
    assert(!brightness_runtime_reference_mark(old.id, true, false, true));
    begin(); step(true); old = snap(); failures = 2;
    assert(!brightness_runtime_reference_step(old.id, false, 5));
    assert(!brightness_runtime_report().output_known && !strcmp(brightness_runtime_report().error, "write_failed"));
    assert(!brightness_runtime_reference_begin()); run_restore_worker_step();
    assert(last == 50 && brightness_runtime_report().output_known);
    begin(); step(false); old = snap(); now_us += 20000000; failures = 1; run_restore_worker_step();
    assert(brightness_runtime_reference_status().phase == BRIGHTNESS_REFERENCE_ERROR && !snap().valid);
    assert(!brightness_runtime_reference_mark(old.id, true, true, false));
    run_restore_worker_step(); assert(last == 50 && !snap().valid);
    assert(!brightness_runtime_ready());
    puts("brightness comparison: previous/current selection, stale tokens, boundaries, deadlines, fault recovery and historical acceptance passed");
}

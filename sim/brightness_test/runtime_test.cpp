#include "brightness_runtime.h"
#include "io_extension.h"
#include "runtime_freertos.h"
#include <cassert>
#include <cstdio>
#include <cstring>
static int64_t now_us; static int writes, last, failure;
int64_t esp_timer_get_time() { return now_us; }
esp_err_t IO_EXTENSION_Pwm_Try_Output(uint8_t value) {
    ++writes;
    if (failure) { --failure; return 9; }
    last=value; return ESP_OK;
}
int main() {
    assert(!brightness_runtime_ready()); assert(!brightness_runtime_preview(50)); assert(writes==0);
    const brightness_board_identity_t board = {"waveshare-esp32-s3-touch-lcd-7b", "measured-rev-a"};
    const brightness_board_identity_t unknown_board = {"unknown", "measured-rev-a"};
    brightness_calibration_profile_t profile = {
        1, 1, "waveshare-esp32-s3-touch-lcd-7b", "measured-rev-a",
        0, 97, 90, 10, 50, true, "schematic-and-baseline-record"
    };
    assert(!brightness_runtime_apply_profile(&profile, &board));
    fail_mutex_creation = true;
    assert(!brightness_runtime_start() && !brightness_runtime_ready() && writes == 0);
    fail_mutex_creation = false;
    fail_task_creation = true;
    assert(!brightness_runtime_start());
    assert(!brightness_runtime_apply_profile(&profile, &board));
    assert(!brightness_runtime_preview(50) && writes == 0);
    fail_task_creation = false;
    assert(brightness_runtime_start());
    assert(brightness_runtime_reference_status().phase == BRIGHTNESS_REFERENCE_DISABLED);
    assert(!brightness_runtime_reference_baseline() && !brightness_runtime_reference_begin() && !brightness_runtime_reference_step(0, true, 5) && writes == 0);
    int created = task_creations;
    assert(brightness_runtime_start() && task_creations == created && writes == 0);
    run_restore_worker_step();
    assert(!brightness_runtime_ready() && writes == 0);
    assert(!brightness_runtime_apply_profile(nullptr, &board));
    assert(!brightness_runtime_apply_profile(&profile, nullptr));
    assert(!brightness_runtime_apply_profile(&profile, &unknown_board));
    brightness_board_identity_t wrong_revision = {"waveshare-esp32-s3-touch-lcd-7b", "unknown-rev"};
    assert(!brightness_runtime_apply_profile(&profile, &wrong_revision));
    assert(!brightness_runtime_ready() && writes==0);
    profile.format_version = 2; assert(!brightness_runtime_apply_profile(&profile, &board));
    profile.format_version = 1;
    profile.profile_revision = 0; assert(!brightness_runtime_apply_profile(&profile, &board));
    profile.profile_revision = 1;
    profile.baseline_verified = false; assert(!brightness_runtime_apply_profile(&profile, &board));
    profile.baseline_verified = true;
    profile.baseline_evidence = ""; assert(!brightness_runtime_apply_profile(&profile, &board));
    profile.baseline_evidence = "schematic-and-baseline-record";
    profile.pwm_at_10 = 50; profile.pwm_at_100 = 50;
    assert(!brightness_runtime_apply_profile(&profile, &board));
    profile.pwm_at_10 = 90; profile.pwm_at_100 = 10;
    profile.pwm_max = 98; assert(!brightness_runtime_apply_profile(&profile, &board));
    profile.pwm_max = 97;
    profile.baseline_pwm = 98; assert(!brightness_runtime_apply_profile(&profile, &board));
    profile.baseline_pwm = 50;
    assert(brightness_runtime_apply_profile(&profile, &board)); assert(brightness_runtime_ready());
    assert(writes==0); // Profile application establishes a baseline but never writes PWM.
    assert(!brightness_runtime_apply_profile(&profile, &board));
    failure=1;
    assert(!brightness_runtime_preview(50) && last==50); // Failed preview is rolled back to the known baseline.
    assert(brightness_runtime_ready() && !strcmp(brightness_runtime_error(), ""));
    assert(brightness_runtime_preview(100) && last==10);
    int preview_writes = writes;
    now_us=1000000;
    assert(!brightness_runtime_preview(50) && writes==preview_writes);
    brightness_checkpoint_t active = {};
    assert(brightness_runtime_checkpoint(&active) && active.deadline_ms==20000 && active.restore==50);
    now_us=19999000; run_restore_worker_step(); assert(last==10);
    now_us=20000000; failure=1; run_restore_worker_step(); assert(last==10);
    assert(!brightness_runtime_ready() && !strcmp(brightness_runtime_error(), "write_failed"));
    assert(!brightness_runtime_preview(50)); // Never stack a new preview over an uncertain output.
    failure=0; run_restore_worker_step(); assert(last==50);
    assert(brightness_runtime_ready());
    assert(brightness_runtime_restore());
    brightness_checkpoint_t checkpoint = {}; assert(brightness_runtime_checkpoint(&checkpoint));
    failure=1; assert(!brightness_runtime_schedule(100,10,true,false) && last==50);
    assert(brightness_runtime_ready() && !strcmp(brightness_runtime_error(), ""));
    assert(brightness_runtime_schedule(100,10,true,false) && last==90);
    assert(brightness_runtime_rollback(&checkpoint) && last==50);
    failure=2; // The preview write and its compensating checkpoint write both fail.
    assert(!brightness_runtime_preview(80) && last==50);
    assert(!brightness_runtime_ready() && !strcmp(brightness_runtime_error(), "write_failed"));
    failure=0;
    run_restore_worker_step(); // Retry the retained checkpoint, despite no active preview.
    assert(last==50 && brightness_runtime_ready());
    brightness_checkpoint_t recovered = {};
    assert(brightness_runtime_checkpoint(&recovered) && !recovered.preview && recovered.current==50);
    failure=1;
    assert(!brightness_runtime_rollback(&recovered) && !brightness_runtime_ready());
    assert(brightness_runtime_restore() && brightness_runtime_ready() && last==50);
    assert(brightness_runtime_preview(70));
    assert(brightness_runtime_restore() && last==50);
    assert(brightness_runtime_checkpoint(&recovered) && !recovered.preview);
    assert(brightness_runtime_preview(80));
    now_us += 20000000;
    failure = 1;
    run_restore_worker_step();
    assert(!brightness_runtime_ready());
    assert(brightness_runtime_restore() && brightness_runtime_ready() && last == 50);
    puts("brightness runtime: startup failures, profile gates, independent worker deadline, non-extending preview, manual restore and retry passed");
}

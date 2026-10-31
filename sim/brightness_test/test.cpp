#include "brightness_controller.h"
#include <assert.h>
#include <stdio.h>
struct fake_t { int value; bool fail; };
static bool write_pwm(uint8_t pwm, void *p) { auto *f = (fake_t*)p; if (f->fail) return false; f->value = pwm; return true; }
int main() {
    brightness_controller_t b = {}; fake_t f = {50, false};
    assert(!brightness_init(&b, write_pwm, &f, 10, 10, 50));
    assert(brightness_init(&b, write_pwm, &f, 90, 10, 50));
    assert(!brightness_set(&b, 9)); assert(brightness_set(&b, 100) && f.value == 10);
    assert(brightness_preview(&b, 10, 100) && f.value == 90);
    assert(!brightness_preview(&b, 50, 200) && b.deadline_ms == 20100 && b.restore == 10);
    assert(brightness_tick(&b, 20099) && b.preview);
    f.fail = true; assert(!brightness_tick(&b, 20100) && b.preview);
    f.fail = false; assert(brightness_tick(&b, 20101) && !b.preview && f.value == 10);
    assert(brightness_apply_schedule(&b, 100, 10, true, false) && f.value == 90);
    assert(brightness_apply_schedule(&b, 100, 10, true, true) && f.value == 10);
    assert(brightness_apply_schedule(&b, 100, 10, false, false) && f.value == 10);
    f.fail = true; assert(!brightness_apply_schedule(&b, 100, 10, true, false) && b.current == 10);
    f.fail = false; assert(brightness_apply_schedule(&b, 100, 10, true, false) && f.value == 90);
    puts("brightness controller: calibration, reverse polarity, bounds, expiry and retry passed");
}

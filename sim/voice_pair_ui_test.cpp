#include "ui_voice_pair.h"
#include "ui_strings.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static int starts;
static int cancels;
static void on_start(void *) { ++starts; }
static void on_cancel(void *) { ++cancels; }

static lv_obj_t *find_button(lv_obj_t *node, int index, int *seen) {
    if (lv_obj_check_type(node, &lv_button_class) && (*seen)++ == index) return node;
    for (uint32_t i = 0; i < lv_obj_get_child_count(node); ++i) {
        if (lv_obj_t *found = find_button(lv_obj_get_child(node, i), index, seen)) return found;
    }
    return nullptr;
}

static void click(lv_obj_t *button) {
    assert(button);
    lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
}

int main() {
    lv_init();
    lv_display_create(1024, 600);
    ui_strings_set_locale("en");
    ui_voice_pair_create(lv_screen_active(), on_start, on_cancel, nullptr);
    assert(!ui_voice_pair_sim_is_visible());
    assert(!ui_voice_pair_sim_has_qr());
    assert(!*ui_voice_pair_sim_code_text());
    ui_voice_pair_show();
    assert(ui_voice_pair_sim_is_visible());

    int seen = 0;
    click(find_button(lv_screen_active(), 0, &seen));
    assert(starts == 1);
    assert(!ui_voice_pair_sim_has_qr()); // Consent alone does not invent code.

    char synthetic[200];
    const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    for (int i = 0; i < 199; ++i) synthetic[i] = alphabet[i % (sizeof(alphabet) - 1)];
    synthetic[199] = '\0';
    assert(!ui_voice_pair_show_code(synthetic, 0, "Ready", "Expires soon"));
    assert(!ui_voice_pair_show_code(synthetic, 200, "Ready", "Expires soon"));
    synthetic[100] = '\n';
    assert(!ui_voice_pair_show_code(synthetic, 199, "Ready", "Expires soon"));
    synthetic[100] = alphabet[100 % (sizeof(alphabet) - 1)];
    assert(ui_voice_pair_show_code(synthetic, 199, "Ready", "Expires soon"));
    assert(ui_voice_pair_sim_has_qr());
    assert(ui_voice_pair_sim_is_visible());
    assert(strlen(ui_voice_pair_sim_code_text()) == 199);
    assert(!ui_voice_pair_sim_start_visible());

    const char replacement[] = "synthetic-replacement-code";
    assert(ui_voice_pair_show_code(replacement, sizeof(replacement) - 1, "Updated", "Still temporary"));
    assert(ui_voice_pair_sim_has_qr());
    assert(!strcmp(ui_voice_pair_sim_code_text(), replacement));
    const char bounded_span[] = {'s', 'y', 'n', 't'};
    assert(ui_voice_pair_show_code(bounded_span, sizeof(bounded_span), "Updated again", "Still temporary"));
    assert(!strcmp(ui_voice_pair_sim_code_text(), "synt"));
    ui_voice_pair_finish("Connected; restart required");
    assert(ui_voice_pair_sim_is_visible());
    assert(!ui_voice_pair_sim_has_qr());
    assert(!*ui_voice_pair_sim_code_text());
    assert(!ui_voice_pair_sim_start_visible());

    seen = 0;
    click(find_button(lv_screen_active(), 1, &seen));
    assert(cancels == 1);
    assert(!ui_voice_pair_sim_has_qr());
    assert(!*ui_voice_pair_sim_code_text());
    assert(!ui_voice_pair_sim_is_visible());

    // Starting again and hiding (device lifecycle/priority transition) clears too.
    ui_voice_pair_show();
    seen = 0; click(find_button(lv_screen_active(), 0, &seen));
    assert(starts == 2);
    assert(ui_voice_pair_show_code("synthetic-test-code", 19, "Ready", "Soon"));
    assert(ui_voice_pair_sim_is_visible());
    ui_voice_pair_error("Unavailable; retry");
    assert(ui_voice_pair_sim_is_visible() && ui_voice_pair_sim_start_visible());
    assert(!ui_voice_pair_sim_has_qr() && !*ui_voice_pair_sim_code_text());
    seen = 0; click(find_button(lv_screen_active(), 0, &seen));
    assert(starts == 3);
    assert(ui_voice_pair_show_code("synthetic-test-code", 19, "Ready", "Soon"));
    ui_voice_pair_hide();
    assert(!ui_voice_pair_sim_has_qr());
    assert(!*ui_voice_pair_sim_code_text());
    assert(!ui_voice_pair_sim_is_visible());
    puts("PASS: consent gate, 199-byte LVGL QR, invalid input, cancel and hide clearing");
    return 0;
}

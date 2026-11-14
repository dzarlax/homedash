// Isolated LVGL fixture for the voice renderer; no dashboard or network hooks.
#include "../src/ui_strings.h"
#include "../src/ui_voice.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

static int dismiss_count;
static int activity_count;
static void dismissed(void *) { ++dismiss_count; }
static void activity(void *) { ++activity_count; }
static void advance_ui(uint32_t ms) { lv_tick_inc(ms); lv_timer_handler(); }

static lv_obj_t *find_button(lv_obj_t *node) {
    if (lv_obj_check_type(node, &lv_button_class)) return node;
    for (uint32_t i = 0; i < lv_obj_get_child_count(node); ++i) {
        if (lv_obj_t *found = find_button(lv_obj_get_child(node, i))) return found;
    }
    return nullptr;
}

int main() {
    lv_init();
    lv_display_create(1024, 600);
    ui_voice_create(lv_screen_active(), dismissed, activity, nullptr);
    ui_voice_state_t state = {UI_VOICE_LISTENING, "", false, false};

    for (const char *locale : {"ru", "en", "sr-Latn"}) {
        ui_strings_set_locale(locale);
        ui_voice_show(&state);
        assert(ui_voice_is_visible());
        if (!strcmp(locale, "en")) assert(!strcmp(ui_voice_sim_status_text(), "Listening"));
        if (!strcmp(locale, "sr-Latn")) assert(!strcmp(ui_voice_sim_status_text(), "Slušam"));
        ui_voice_state_t truncated = {UI_VOICE_ANSWER, "Sample", false, true};
        ui_voice_update(&truncated);
        const char *expected = !strcmp(locale, "en") ? "Response shortened" :
                               !strcmp(locale, "sr-Latn") ? "Odgovor je skraćen" : "Ответ сокращён";
        const char *speech = !strcmp(locale, "en") ? "Assistant speech" :
                             !strcmp(locale, "sr-Latn") ? "Govor asistenta" : "Речь ассистента";
        assert(!strcmp(ui_voice_sim_answer_text(), "Sample"));
        assert(!strcmp(ui_voice_sim_status_text(), speech)); // Answer phase carries origin once.
        assert(!*ui_voice_sim_speech_text()); // No duplicate label.
        assert(!strcmp(ui_voice_sim_truncated_text(), expected));

        ui_voice_state_t processing = {UI_VOICE_PROCESSING, "Status text", false, false};
        ui_voice_update(&processing);
        const char *processing_status = !strcmp(locale, "en") ? "Working on your request" :
                                        !strcmp(locale, "sr-Latn") ? "Obrađujem zahtev" : "Обрабатываю запрос";
        assert(!strcmp(ui_voice_sim_status_text(), processing_status));
        assert(!strcmp(ui_voice_sim_speech_text(), speech)); // Other phases keep separate origin label.
    }
    ui_strings_set_locale("en");
    state = {UI_VOICE_ANSWER, "", false, true};
    ui_voice_update(&state);
    assert(!*ui_voice_sim_answer_text());
    assert(!*ui_voice_sim_speech_text());
    assert(!strcmp(ui_voice_sim_truncated_text(), "Response shortened"));
    state = {UI_VOICE_APPROVAL_REQUIRED, "", false, true};
    ui_voice_update(&state);
    assert(!*ui_voice_sim_answer_text());
    assert(!*ui_voice_sim_speech_text());
    assert(!*ui_voice_sim_truncated_text()); // Sensitive phases stay neutral.

    std::string long_answer;
    for (int i = 0; i < 100; ++i) long_answer += "Длинный ответ, который должен переноситься на новую строку. ";
    ui_strings_set_locale("ru");
    state = {UI_VOICE_ANSWER, long_answer.c_str(), false, false};
    ui_voice_update(&state);
    assert(!strcmp(ui_voice_sim_status_text(), "Речь ассистента"));
    assert(!*ui_voice_sim_speech_text());
    lv_obj_update_layout(lv_screen_active());
    assert(strlen(ui_voice_sim_answer_text()) == long_answer.size());
    assert(ui_voice_sim_at_bottom()); // New text follows the tail initially.
    ui_voice_sim_scroll_up();
    assert(!ui_voice_sim_at_bottom());
    long_answer += "Новое продолжение.";
    state.text = long_answer.c_str();
    state.text_truncated = true;
    ui_voice_update(&state);
    assert(!ui_voice_sim_at_bottom()); // A manual scroll is preserved.
    assert(!strcmp(ui_voice_sim_answer_text(), long_answer.c_str())); // Body stays intact.
    for (const char *locale : {"ru", "en", "sr-Latn"}) {
        ui_strings_set_locale(locale);
        ui_voice_update(&state);
        const char *expected = !strcmp(locale, "en") ? "Response shortened" :
                               !strcmp(locale, "sr-Latn") ? "Odgovor je skraćen" : "Ответ сокращён";
        assert(!strcmp(ui_voice_sim_truncated_text(), expected));
        const char *speech = !strcmp(locale, "en") ? "Assistant speech" :
                             !strcmp(locale, "sr-Latn") ? "Govor asistenta" : "Речь ассистента";
        assert(!strcmp(ui_voice_sim_status_text(), speech));
        assert(!*ui_voice_sim_speech_text()); // Answer phase carries origin only once.
        assert(!ui_voice_sim_at_bottom()); // Notice stays outside the scrolled answer.
        assert(!strcmp(ui_voice_sim_answer_text(), long_answer.c_str()));
    }

    ui_strings_set_locale("sr-Latn");
    const std::string interrupted_speech = long_answer.substr(0, 120) +
        "\n— Прервано —\n" + long_answer.substr(120) + "Kasniji tekst.";
    state.text = interrupted_speech.c_str();
    state.interrupted = true;
    ui_voice_update(&state);
    assert(!strcmp(ui_voice_sim_answer_text(), interrupted_speech.c_str())); // Wire text renders verbatim.
    const char *marker = strstr(ui_voice_sim_answer_text(), "\n— Прервано —\n");
    assert(marker && marker > ui_voice_sim_answer_text());
    assert(strstr(marker + strlen("\n— Прервано —\n"), "Kasniji tekst.")); // Later fragment stays after marker.
    assert(!ui_voice_sim_at_bottom()); // Separator scrolls with the answer.

    std::string boundary_text(8189, 'x');
    boundary_text += "€"; // Exactly 8192 UTF-8 bytes, ending on a complete code point.
    assert(boundary_text.size() == 8192);
    state = {UI_VOICE_ANSWER, boundary_text.c_str(), true, false};
    ui_voice_update(&state);
    const char *interrupted_answer = ui_voice_sim_answer_text();
    assert(strlen(interrupted_answer) == boundary_text.size());
    assert(!memcmp(interrupted_answer, boundary_text.data(), boundary_text.size()));
    state = {UI_VOICE_OFFLINE, nullptr, false, false};
    ui_voice_update(&state);
    assert(!strcmp(ui_voice_sim_status_text(), "Veza je prekinuta"));
    assert(!*ui_voice_sim_speech_text());

    lv_obj_t *button = find_button(lv_screen_active());
    assert(button);
    lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
    assert(dismiss_count == 1 && !ui_voice_is_visible()); // Hide is requested as soon as fade-out starts.
    assert(!*ui_voice_sim_answer_text()); // Sensitive content clears immediately.
    advance_ui(200);
    assert(!ui_voice_is_visible());
    ui_voice_show(&state);
    assert(ui_voice_is_visible());
    state = {UI_VOICE_ANSWER, "Sensitive response", false, false};
    ui_voice_update(&state);
    ui_voice_hide();
    assert(!ui_voice_is_visible());
    assert(!ui_voice_sim_physically_hidden()); // Fade has started; the root remains drawable.
    assert(!*ui_voice_sim_answer_text());
    ui_voice_hide_immediately();
    assert(!ui_voice_is_visible());
    assert(ui_voice_sim_physically_hidden()); // Priority hide interrupts the fade.
    assert(!*ui_voice_sim_answer_text());
    ui_voice_show(&state);
    assert(ui_voice_is_visible());
    assert(!ui_voice_sim_physically_hidden());
    advance_ui(200);
    assert(ui_voice_is_visible()); // A canceled fade cannot hide a later show.
    ui_voice_hide_immediately();
    std::puts("voice overlay fixture passed");
}

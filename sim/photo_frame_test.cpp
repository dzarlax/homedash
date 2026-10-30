#include "../src/photo_frame.cpp"
#include "../src/ui_dashboard.cpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

extern int photo_test_alloc_fail_after;
static int64_t fake_seconds = 0;
static bool pointer_down = false;
static int pointer_x = 10, pointer_y = 10;
static int clicks = 0;
static void pointer_read(lv_indev_t *, lv_indev_data_t *data) {
  photo_frame_filter_touch(pointer_down, pointer_x, pointer_y, data);
}
static void clicked(lv_event_t *) { clicks++; }
static void pointer(lv_indev_t *indev, bool down, int x, int y) {
  pointer_down = down;
  pointer_x = x;
  pointer_y = y;
  lv_tick_inc(40);
  lv_indev_read(indev);
}
static lv_area_t control_coords(int index) {
  assert(control_buttons[index]);
  lv_obj_update_layout(overlay);
  lv_area_t coords = {};
  lv_obj_get_coords(control_buttons[index], &coords);
  return coords;
}
static void assert_controls_on_screen() {
  assert(control_bar && controls_visible);
  lv_obj_update_layout(overlay);
  lv_area_t bar = {};
  lv_obj_get_coords(control_bar, &bar);
  assert(bar.x1 >= 0 && bar.y1 >= 0 && bar.x2 < 1024 && bar.y2 < 600);
  const int count = load_error ? 6 : 5;
  for (int i = 0; i < count; ++i) {
    assert(!lv_obj_has_flag(control_buttons[i], LV_OBJ_FLAG_HIDDEN));
    lv_area_t coords = control_coords(i);
    assert(coords.x1 >= 0 && coords.y1 >= 0 && coords.x2 < 1024 && coords.y2 < 600);
    assert(coords.x2 - coords.x1 + 1 >= 44 && coords.y2 - coords.y1 + 1 >= 44);
    int action = i == count - 1 ? 5 : i;
    assert(control_at((coords.x1 + coords.x2) / 2, (coords.y1 + coords.y2) / 2) == action);
  }
}
static void tap_control(lv_indev_t *indev, int index) {
  lv_area_t coords = control_coords(index);
  int x = (coords.x1 + coords.x2) / 2, y = (coords.y1 + coords.y2) / 2;
  pointer(indev, true, x, y);
  pointer(indev, false, x, y);
}

extern "C" int64_t esp_timer_get_time() { return fake_seconds * 1000000; }
unsigned ulTaskNotifyTake(int, unsigned) { return 0; }
static bool acknowledge_worker = false;
void vTaskDelay(unsigned) {
  ++fake_seconds;
  if (acknowledge_worker)
    busy.store(false);
}
int xTaskCreatePinnedToCore(void (*)(void *), const char *, unsigned, void *,
                            unsigned, TaskHandle_t *out, int) {
  *out = (void *)1;
  return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t) {}
SemaphoreHandle_t xSemaphoreCreateMutex() { return (void *)1; }
int xSemaphoreTake(SemaphoreHandle_t, unsigned) { return pdTRUE; }
int xSemaphoreGive(SemaphoreHandle_t) { return pdTRUE; }
bool lvgl_port_lock(int) { return true; }
void lvgl_port_unlock() {}
photo_result photo_fetch_manifest(const char *, const char *, photo_manifest *,
                                  photo_cancel_fn, void *, photo_diagnostic *) {
  return PHOTO_FAILED;
}
photo_result photo_fetch_pixels(const char *, const char *,
                                const photo_frame_entry *, uint8_t *,
                                photo_cancel_fn, void *, photo_diagnostic *) {
  return PHOTO_FAILED;
}
photo_result photo_fetch_context(const char *, const char *, photo_context *, photo_cancel_fn, void *, photo_diagnostic *, int, const char *) { return PHOTO_FAILED; }
photo_result photo_fetch_overlay(const char *, const char *, const photo_overlay_block *, uint8_t *, photo_cancel_fn, void *, photo_diagnostic *, int) { return PHOTO_FAILED; }
static void complete_context_404() { if (busy && job.kind == CONTEXT) { job.result = PHOTO_NOT_FOUND; job.complete = true; busy = false; tick(nullptr); } }
static void complete_manifest() {
  assert(busy.load() && job.kind == MANIFEST);
  job.manifest = {};
  strcpy(job.manifest.state, "ready");
  job.manifest.idle = 300;
  job.manifest.interval = 60;
  job.manifest.stale = 900;
  job.manifest.count = 2;
  memset(job.manifest.frames[0].id, 'a', 64);
  memset(job.manifest.frames[1].id, 'b', 64);
  job.result = PHOTO_OK;
  job.complete = true;
  busy = false;
}
static void complete_frame(photo_result r = PHOTO_OK) {
  assert(busy.load() && job.kind == FRAME);
  job.result = r;
  job.complete = true;
  busy = false;
}
int main() {
  lv_init();
  lv_display_create(1024, 600);
  ui_dashboard_create();
  photo_frame_ui_init();
  photo_frame_network_init("fixture", "fixture");
  lv_obj_t *button = lv_button_create(lv_screen_active());
  lv_obj_set_size(button, 100, 100);
  lv_obj_set_pos(button, 0, 0);
  lv_obj_add_event_cb(button, clicked, LV_EVENT_CLICKED, nullptr);
  lv_obj_update_layout(lv_screen_active());
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, pointer_read);
  fake_seconds = 299;
  tick(nullptr);
  assert(!busy.load());
  fake_seconds = 300;
  tick(nullptr);
  assert(job.kind == MANIFEST);
  complete_manifest();
  tick(nullptr);
  assert(job.kind == FRAME);
  complete_frame();
  ui_dashboard_open_settings();
  ui_dashboard_update_recovery("HomeDash-Setup-DEMO", "TestOnlyPassword", "http://192.168.4.1");
  reveal_password_cb(nullptr);
  assert(settings_reveal_until_us);
  tick(nullptr);
  assert(!settings_reveal_until_us);
  assert(!strcmp(lv_label_get_text(settings_password), "****************"));
  assert(overlay && displayed >= 0);
  assert(shown_id[0] == 'a');
  complete_context_404();
  // Photo taps reveal a timed control strip; subsequent background taps hide
  // it. Every contact stays inside the photo mode and never reaches dashboard.
  lv_obj_t *initial_overlay = overlay;
  if (controls_visible) { pointer(indev, true, 10, 10); pointer(indev, false, 10, 10); }
  pointer(indev, true, 10, 10); pointer(indev, false, 10, 10);
  assert(overlay == initial_overlay && controls_visible && clicks == 0);
  assert_controls_on_screen();
  pointer(indev, true, 80, 80); pointer(indev, false, 80, 80);
  assert(overlay == initial_overlay && !controls_visible && clicks == 0);
  pointer(indev, true, 10, 10); pointer(indev, false, 10, 10);
  fake_seconds += 5; tick(nullptr);
  assert(!controls_visible && overlay == initial_overlay);
  // A horizontal swipe advances in manifest order and replaces only after the
  // requested image is ready.
  pointer(indev, true, 300, 240); pointer(indev, false, 380, 245);
  assert(manual_target_pending && manual_target_id[0] == 'b');
  tick(nullptr); assert(busy && job.kind == FRAME && job.frame.id[0] == 'b');
  unsigned obsolete_generation = job.generation;
  pointer(indev, true, 380, 240); pointer(indev, false, 300, 245);
  assert(!manual_target_pending && shown_id[0] == 'a' && epoch.load() != obsolete_generation);
  complete_frame(); tick(nullptr);
  assert(shown_id[0] == 'a' && overlay == initial_overlay && !manual_target_pending);
  // A fresh next request can proceed after the obsolete response is discarded.
  pointer(indev, true, 300, 240); pointer(indev, false, 380, 245);
  assert(manual_target_pending && manual_target_id[0] == 'b');
  tick(nullptr); assert(busy && job.kind == FRAME && job.frame.id[0] == 'b');
  complete_frame(); tick(nullptr); assert(shown_id[0] == 'b');
  assert(overlay == initial_overlay);
  complete_context_404();
  // Pause stops timed changes but manual navigation continues to work.
  assert_controls_on_screen();
  tap_control(indev, 2);
  assert(slideshow_paused);
  pointer(indev, true, 400, 240); pointer(indev, false, 480, 240);
  assert(manual_target_pending && manual_target_id[0] == 'a');
  tick(nullptr); assert(busy && job.kind == FRAME && job.frame.id[0] == 'a');
  complete_frame(); tick(nullptr); assert(shown_id[0] == 'a' && slideshow_paused);
  complete_context_404();
  execute_control(5);
  assert(!overlay && !slideshow_paused && !info_hidden);
  // An unfinished transfer from the previous idle generation cannot publish.
  fake_seconds = 600;
  last_poll = fake_seconds - 60;
  last_touch = fake_seconds - idle_seconds;
  tick(nullptr);
  assert(job.kind == MANIFEST);
  unsigned old = job.generation;
  assert(!photo_frame_touch(true));
  assert(epoch.load() != old);
  photo_frame_touch(false);
  complete_manifest();
  tick(nullptr);
  assert(!overlay);
  // Reader prevents entry and close resets idle.
  open_reader("fixture", "fixture", "fixture", nullptr, nullptr);
  fake_seconds = 1000;
  tick(nullptr);
  assert(!busy.load());
  reader_close_cb(nullptr);
  fake_seconds = 1299;
  tick(nullptr);
  assert(!busy.load());
  fake_seconds = 1300;
  tick(nullptr);
  assert(job.kind == MANIFEST);
  complete_manifest();
  tick(nullptr);
  complete_frame(PHOTO_FAILED);
  job.diagnostic = {"deadline", 200, 0, 8192};
  tick(nullptr);
  assert(!overlay);
  assert(retry_after > fake_seconds);
  fake_seconds += 60;
  tick(nullptr);
  assert(job.kind == MANIFEST);
  complete_manifest();
  tick(nullptr);
  complete_frame();
  tick(nullptr);
  assert(overlay);
  // Just before source expiry the current photo remains visible. Once it
  // expires, stale context/manifest data is stopped while Retry stays available.
  lv_obj_t *stale_overlay = overlay;
  slideshow_paused = true;
  last_ready = fake_seconds - stale_seconds + 1;
  last_poll = fake_seconds;
  busy = false;
  job.complete = false;
  tick(nullptr);
  assert(overlay == stale_overlay && manifest_ready && !busy.load());
  last_ready = fake_seconds - stale_seconds;
  tick(nullptr);
  assert(overlay == stale_overlay && !manifest_ready && load_error);
  assert(busy && job.kind == MANIFEST && stale_refresh_pending);
  job.result = PHOTO_FAILED; job.complete = true; busy = false; tick(nullptr);
  assert(overlay == stale_overlay && load_error && retry_requested);
  pointer(indev, true, 10, 10); pointer(indev, false, 10, 10);
  assert(controls_visible && control_status && !lv_obj_has_flag(control_status, LV_OBJ_FLAG_HIDDEN));
  assert_controls_on_screen();
  tap_control(indev, 4);
  assert(retry_requested && !stale_refresh_pending);
  tick(nullptr); assert(busy && job.kind == MANIFEST);
  complete_manifest(); tick(nullptr);
  assert(overlay == stale_overlay && manifest_ready && shown_id[0] == 'a');
  if (busy && job.kind == CONTEXT) complete_context_404();
  assert(busy && job.kind == FRAME && job.frame.id[0] == 'b');
  complete_frame(); tick(nullptr);
  assert(overlay == stale_overlay && shown_id[0] == 'b' && !load_error && !retry_requested);
  complete_context_404();
  // The expired manifest never starts a frame until a fresh ready response.
  assert(manifest_ready && overlay == stale_overlay);
  slideshow_paused = false;
  // Slide timing uses the playlist between manifest polls.
  fake_seconds = 3000;
  last_touch = fake_seconds - 300;
  last_poll = fake_seconds;
  last_ready = fake_seconds;
  retry_after = 0;
  interval_seconds = 15;
  current_manifest = {};
  current_manifest.count = 2;
  memset(current_manifest.frames[0].id, 'a', 64);
  memset(current_manifest.frames[1].id, 'b', 64);
  manifest_ready = true;
  assert(allocate_buffers());
  publish(0, current_manifest.frames[0].id);
  context_poll = fake_seconds;
  fake_seconds += 14;
  tick(nullptr);
  assert(!busy.load());
  fake_seconds++;
  tick(nullptr);
  assert(busy.load() && job.kind == FRAME);
  assert(job.frame.id[0] == 'b');
  complete_frame();
  tick(nullptr);
  assert(shown_id[0] == 'b');
  complete_context_404();
  // A one-frame playlist must not download the same frame again.
  current_manifest.frames[0] = current_manifest.frames[1];
  current_manifest.count = 1;
  fake_seconds += 15;
  tick(nullptr);
  assert(!busy.load());
  // Disabled and empty manifests release both buffers after worker completion.
  for (const char *state : {"disabled", "empty"}) {
    assert(allocate_buffers());
    publish(0, current_manifest.frames[0].id);
    assert(submit(MANIFEST, 0, nullptr));
    complete_manifest();
    strcpy(job.manifest.state, state);
    job.manifest.count = 0;
    tick(nullptr);
    assert(!overlay && !buffers[0] && !buffers[1] && !manifest_ready);
  }
  // A worker that misses the OTA deadline retains buffers; after
  // acknowledgement the photo UI and buffers are released and the cached
  // manifest is cleared.
  assert(allocate_buffers());
  publish(0, current_manifest.frames[0].id);
  manifest_ready = true;
  busy = true;
  acknowledge_worker = false;
  assert(!photo_frame_pause_for_ota());
  assert(buffers[0] && buffers[1]);
  acknowledge_worker = true;
  assert(photo_frame_pause_for_ota());
  assert(!buffers[0] && !buffers[1]);
  assert(!manifest_ready && !overlay);
  photo_frame_resume_after_ota();
  // The web command starts without waiting for the idle threshold.
  fake_seconds = 5000;
  last_touch = fake_seconds;
  last_poll = fake_seconds;
  retry_after = fake_seconds + 300;
  lv_tileview_set_tile(tileview, nav_tiles[2], LV_ANIM_OFF);
  lv_obj_update_layout(lv_screen_active());
  assert(photo_frame_start_now());
  assert(busy.load() && job.kind == MANIFEST);
  assert(manual_requested);
  complete_manifest();
  tick(nullptr);
  assert(busy.load() && job.kind == FRAME);
  complete_frame();
  tick(nullptr);
  assert(overlay);
  complete_context_404();
  assert(lv_tileview_get_tile_active(tileview) == nav_tiles[2]);
  // Exit is explicit, and the same gesture cannot relaunch the frame.
  pointer(indev, true, 740, 20);
  pointer(indev, false, 740, 20);
  assert_controls_on_screen();
  tap_control(indev, 4);
  assert(!overlay && !manual_requested && !busy.load());
  // A failed manual attempt leaves photo mode closed.
  assert(photo_frame_start_now());
  assert(busy.load() && job.kind == MANIFEST);
  job.result = PHOTO_FAILED;
  job.diagnostic = {"HTTP", 401, 0};
  job.complete = true;
  busy = false;
  tick(nullptr);
  assert(!overlay);
  photo_frame_touch(true);
  photo_frame_touch(false);
  // A touch while a manual frame is loading cancels the force override too.
  assert(photo_frame_start_now());
  complete_manifest();
  tick(nullptr);
  assert(busy.load() && job.kind == FRAME);
  photo_frame_touch(true);
  photo_frame_touch(false);
  complete_frame();
  tick(nullptr);
  assert(!overlay && !manual_requested);
  assert(photo_frame_start_now());
  complete_manifest();
  strcpy(job.manifest.state, "disabled");
  job.manifest.count = 0;
  tick(nullptr);
  assert(!overlay && !manual_requested);
  paused = true;
  assert(!photo_frame_start_now());
  paused = false;
  // Keep the current image visible throughout manifest refresh, frame loading
  // and a failed next-frame transfer; replace its source only on success.
  assert(photo_frame_start_now());
  complete_manifest(); tick(nullptr); complete_frame(); tick(nullptr);
  complete_context_404();
  lv_obj_t *visible_overlay = overlay;
  assert(visible_overlay && shown_id[0] == 'a');
  fake_seconds += 60; tick(nullptr);
  complete_context_404();
  assert(busy.load() && job.kind == MANIFEST && overlay == visible_overlay);
  complete_manifest(); tick(nullptr);
  assert(busy.load() && job.kind == FRAME && overlay == visible_overlay);
  complete_frame(PHOTO_FAILED); tick(nullptr);
  assert(overlay == visible_overlay && shown_id[0] == 'a' && load_error);
  pointer(indev, true, 10, 10); pointer(indev, false, 10, 10);
  assert(controls_visible && control_status && !lv_obj_has_flag(control_status, LV_OBJ_FLAG_HIDDEN));
  assert_controls_on_screen();
  tap_control(indev, 4);
  assert(!load_error && retry_requested);
  tick(nullptr); assert(busy && job.kind == MANIFEST);
  complete_manifest(); tick(nullptr);
  assert(busy.load() && job.kind == FRAME && job.frame.id[0] == 'b' && overlay == visible_overlay);
  complete_frame(); tick(nullptr);
  assert(overlay == visible_overlay && shown_id[0] == 'b' && !retry_requested);
  // Context changes do not reset the current image or slide time; card failures
  // retain non-expired pixels, and expiry uses monotonic server time.
  complete_context_404();
  int64_t slide_before = last_slide;
  context_poll = 0; tick(nullptr); assert(job.kind == CONTEXT);
  web_preferences_t clock_prefs = {}; web_preferences_defaults(&clock_prefs);
  clock_prefs.revision = 1; strcpy(clock_prefs.settings_url, "https://example.test/settings/");
  ui_dashboard_apply_preferences(&clock_prefs);
  photo_frame_confirm_timezone(clock_prefs.timezone, clock_prefs.posix_timezone);
  job.context = {}; job.context.server_time = 1793361600; job.context.count = 1;
  job.context.clock_enabled = true; strcpy(job.context.locale, "en");
  strcpy(job.context.timezone, clock_prefs.timezone); strcpy(job.context.clock_format, "12h");
  strcpy(job.context.blocks[0].kind, "weather"); memset(job.context.blocks[0].id, 'c', 64);
  job.context.blocks[0].width = 10; job.context.blocks[0].height = 10;
  job.context.blocks[0].expires_at = 1793361700;
  job.result = PHOTO_OK; job.complete = true; busy = false; tick(nullptr);
  assert(job.kind == CARD && job.staging); memset(job.staging, 0, 200);
  job.result = PHOTO_OK; job.complete = true; busy = false; tick(nullptr);
  assert(cards[0].pixels && overlay == visible_overlay && last_slide == slide_before);
  assert(clock_label && date_label && !lv_obj_has_flag(clock_label, LV_OBJ_FLAG_HIDDEN));
  if (controls_visible) { pointer(indev, true, 10, 10); pointer(indev, false, 10, 10); }
  pointer(indev, true, 10, 10); pointer(indev, false, 10, 10);
  assert_controls_on_screen();
  tap_control(indev, 3);
  assert(info_hidden && lv_obj_has_flag(cards[0].object, LV_OBJ_FLAG_HIDDEN));
  assert(lv_obj_has_flag(clock_label, LV_OBJ_FLAG_HIDDEN) && lv_obj_has_flag(date_label, LV_OBJ_FLAG_HIDDEN));
  tap_control(indev, 3);
  assert(!info_hidden && !lv_obj_has_flag(cards[0].object, LV_OBJ_FLAG_HIDDEN));
  assert(!lv_obj_has_flag(clock_label, LV_OBJ_FLAG_HIDDEN));
  for (int i = 0; i < 4; ++i) {
    assert(clock_shadow[i] && date_shadow[i]);
    assert(lv_obj_get_index(clock_shadow[i]) < lv_obj_get_index(clock_label));
    assert(!strcmp(lv_label_get_text(clock_shadow[i]), lv_label_get_text(clock_label)));
  }
  lv_obj_update_layout(overlay);
  assert(lv_obj_get_x(clock_shadow[0]) == 37 && lv_obj_get_x(clock_shadow[1]) == 39);
  assert(lv_obj_get_y(clock_shadow[2]) == 474 && lv_obj_get_y(clock_shadow[3]) == 476);
  assert(strstr(lv_label_get_text(clock_label), "AM") || strstr(lv_label_get_text(clock_label), "PM"));
  assert(lv_obj_get_width(clock_label) < 468);
  strcpy(confirmed_timezone, "UTC"); render_clock();
  assert(lv_obj_has_flag(clock_label, LV_OBJ_FLAG_HIDDEN));
  photo_frame_confirm_timezone(clock_prefs.timezone, clock_prefs.posix_timezone); render_clock();
  assert(!lv_obj_has_flag(clock_label, LV_OBJ_FLAG_HIDDEN));
  context_poll = 0; tick(nullptr); assert(job.kind == CONTEXT);
  job.result = PHOTO_FAILED; job.complete = true; busy = false; tick(nullptr);
  assert(cards[0].pixels && overlay == visible_overlay && last_slide == slide_before);
  fake_seconds += 101; last_ready = fake_seconds; last_poll = fake_seconds; last_slide = fake_seconds; context_poll = fake_seconds;
  tick(nullptr); assert(!cards[0].pixels && overlay == visible_overlay);
  // A context delayed in transit must not extend an already expired card.
  context_poll = 0; tick(nullptr); assert(job.kind == CONTEXT);
  job.context = {}; job.context.server_time = 1793362000; job.context.count = 1;
  strcpy(job.context.blocks[0].kind, "event"); memset(job.context.blocks[0].id, 'd', 64);
  job.context.blocks[0].width = 10; job.context.blocks[0].height = 10;
  job.context.blocks[0].expires_at = 1793362005;
  fake_seconds += 6; job.result = PHOTO_OK; job.complete = true; busy = false; tick(nullptr);
  assert(!cards[1].pixels && (!busy || job.kind != CARD));
  // Late frame-bound metadata and pixels cannot affect the successor frame.
  if (busy) { job.result = PHOTO_CANCELLED; job.complete = true; busy = false; tick(nullptr); }
  current_manifest.overlay_context_version = 2; context_poll = 0; tick(nullptr);
  assert(busy && job.kind == CONTEXT); char previous[65]; strcpy(previous, shown_id);
  char next[65]; memset(next, 'e', 64); next[64] = 0;
  publish(displayed == 0 ? 1 : 0, next); int64_t changed_at = last_slide;
  job.context = {}; job.context.clock_enabled = true; strcpy(job.context.tone, "dark");
  job.result = PHOTO_OK; job.complete = true; busy = false; tick(nullptr);
  assert(!context_known && !cards[0].pixels && !strcmp(shown_id, next) && last_slide == changed_at);
  if (busy) { job.result = PHOTO_CANCELLED; job.complete = true; busy = false; tick(nullptr); }
  context_poll = esp_timer_get_time() / 1000000; wanted = {}; context_known = true;
  wanted.count = 1; strcpy(wanted.blocks[0].kind, "weather"); memset(wanted.blocks[0].id, 'f', 64);
  wanted.blocks[0].width = 10; wanted.blocks[0].height = 10; wanted.blocks[0].expires_at = server_now() + 100;
  uint8_t *late = (uint8_t *)malloc(200); assert(submit(CARD, 0, nullptr, &wanted.blocks[0], late));
  strcpy(job.bound_frame, previous); job.result = PHOTO_OK; job.complete = true; busy = false;
  card_retry = esp_timer_get_time() / 1000000 + 60; tick(nullptr);
  assert(!cards[0].pixels && last_slide == changed_at);
  // Protocol changes on the same slide clear old cards and immediately fetch
  // using the negotiated version, without moving the slide deadline.
  current_manifest.frames[0] = {}; strcpy(current_manifest.frames[0].id, next);
  current_manifest.count = 1; current_manifest.overlay_context_version = 1;
  for (int target_version : {2, 1}) {
    context_known = true; strcpy(wanted.tone, "dark");
    cards[0].pixels = (uint8_t *)malloc(200); assert(cards[0].pixels);
    context_poll = esp_timer_get_time() / 1000000;
    assert(submit(MANIFEST, 0, nullptr)); complete_manifest();
    job.manifest.count = 1; strcpy(job.manifest.frames[0].id, next);
    job.manifest.overlay_context_version = target_version;
    int64_t unchanged_slide = last_slide; tick(nullptr);
    assert(!context_known && !cards[0].pixels && last_slide == unchanged_slide && overlay);
    assert(busy && job.kind == CONTEXT && job.version == target_version);
    job.result = PHOTO_NOT_FOUND; job.complete = true; busy = false; tick(nullptr);
  }
  // An in-flight v1 context is invalidated immediately on network handoff.
  current_manifest.overlay_context_version = 1;
  assert(submit(CONTEXT, 0, nullptr)); unsigned old_network_epoch = job.generation;
  photo_frame_update_network("new-bridge", "new-key"); tick(nullptr);
  assert(!context_known && !manifest_ready && overlay && busy && !strcmp(shown_id, next));
  assert(epoch != old_network_epoch);
  job.context = {}; job.context.clock_enabled = true; job.result = PHOTO_OK;
  job.complete = true; busy = false; tick(nullptr);
  assert(!context_known && overlay && busy && job.kind == MANIFEST);
  // The v2 manifest lacks the retained photo: no context or card can be fetched.
  complete_manifest(); job.manifest.overlay_context_version = 2; tick(nullptr);
  assert(manifest_ready && !shown_in_manifest() && !context_known && overlay && !busy);
  context_poll = 0; tick(nullptr); assert(!busy && !cards[0].pixels);
  // Once this same photo is included by the negotiated Bridge, v2 context is allowed.
  current_manifest.frames[0] = {}; strcpy(current_manifest.frames[0].id, next);
  tick(nullptr); assert(busy && job.kind == CONTEXT && job.version == 2 && !strcmp(job.bound_frame, next));
  job.result = PHOTO_NOT_FOUND; job.complete = true; busy = false; tick(nullptr);
  // A disabled photo frame retains buffers while the cancelled worker owns them.
  busy = true; web_photos_enabled = false; tick(nullptr);
  assert(buffers[0] && buffers[1]);
  busy = false; tick(nullptr); assert(!buffers[0] && !buffers[1]);

  // The measured clock envelope must contain every displayed minute and date.
  int widths[6]; measure_clock_widths(widths); std::printf("v3 clock widths: %d,%d,%d,%d,%d,%d\n", widths[0], widths[1], widths[2], widths[3], widths[4], widths[5]); char text[128]; lv_point_t size;
  for (int lang = 0; lang < 3; ++lang) for (int twelve = 0; twelve < 2; ++twelve) {
    assert(widths[lang * 2 + twelve] >= 144 && widths[lang * 2 + twelve] <= 480);
    for (int minute = 0; minute < 1440; ++minute) {
      format_time(text, sizeof(text), minute / 60, minute % 60, twelve);
      lv_text_get_size(&size, text, &font_photo_clock_80, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
      assert(size.x + 34 <= widths[lang * 2 + twelve]);
    }
    for (int d = 1; d <= 31; ++d) for (int w = 0; w < 7; ++w) for (int m = 0; m < 12; ++m) {
      format_date(text, sizeof(text), lang, d, w, m);
      lv_text_get_size(&size, text, &font_montserrat_16_cyr, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
      assert(size.x + 34 <= widths[lang * 2 + twelve]);
    }
  }
  // v3 cards and clock persist while three independent photos change.
  web_photos_enabled = true; manual_requested = true; retry_after = 0; card_retry = 0; last_poll = fake_seconds;
  current_manifest = {}; strcpy(current_manifest.state, "ready");
  current_manifest.scene_context_version = 3; current_manifest.overlay_context_version = 2;
  current_manifest.count = 3;
  for (int i = 0; i < 3; ++i) memset(current_manifest.frames[i].id, 'a' + i, 64);
  manifest_ready = true; protocol_negotiated = true; last_ready = fake_seconds; tick(nullptr);
  assert(busy && job.kind == FRAME && !overlay); complete_frame(); tick(nullptr);
  assert(overlay && busy && job.kind == CONTEXT && job.version == 3);
  job.context = {}; job.context.version = 3; job.context.server_time = 1793361600;
  job.context.count = 1; job.context.clock_enabled = true; strcpy(job.context.locale, "en");
  strcpy(job.context.clock_format, "24h"); strcpy(job.context.timezone, clock_prefs.timezone);
  strcpy(job.context.blocks[0].kind, "weather"); memset(job.context.blocks[0].id, 'c', 64);
  job.context.blocks[0].width = 10; job.context.blocks[0].height = 10;
  job.context.blocks[0].expires_at = 1793362500;
  job.result = PHOTO_OK; job.complete = true; busy = false; tick(nullptr);
  assert(busy && job.kind == CARD); memset(job.staging, 0, 400);
  job.result = PHOTO_OK; job.complete = true; busy = false; tick(nullptr);
  assert(cards[0].pixels && clock_panel);
  auto *pixels = cards[0].pixels; auto *panel = cards[0].object; auto *clock = clock_panel;
  for (int i = 0; i < 3; ++i) {
    fake_seconds += 45; last_ready = fake_seconds; last_poll = fake_seconds; tick(nullptr);
    assert(busy && job.kind == FRAME); complete_frame(); tick(nullptr);
    assert(pending.ready && cards[0].pixels == pixels && cards[0].object == panel && clock_panel == clock);
    // A card/context request can remain in flight at the slide deadline.
    if (!busy) { context_poll = 0; tick(nullptr); }
    assert(busy && job.kind == CONTEXT);
    fake_seconds += 15; last_ready = fake_seconds; last_poll = fake_seconds; tick(nullptr);
    assert(!pending.ready && cards[0].pixels == pixels && cards[0].object == panel && clock_panel == clock);
    if (i == 0) { job.context = wanted; job.context.server_time = server_now(); job.result = PHOTO_OK; }
    else job.result = PHOTO_FAILED;
    job.complete = true; busy = false; tick(nullptr);
    assert(cards[0].pixels == pixels && cards[0].object == panel && clock_panel == clock && (!busy || job.kind != CARD));
  }
  // Non-ready photo sources retain independent context polling after negotiation.
  photo_manifest saved_manifest = current_manifest; saved_manifest.stale = 900; saved_manifest.idle = 300; saved_manifest.interval = 60;
  for (const char *state : {"warming", "unavailable"}) {
    assert(submit(MANIFEST, 0, nullptr));
    job.manifest = saved_manifest; strcpy(job.manifest.state, state); job.manifest.count = 0;
    job.result = PHOTO_OK; job.complete = true; busy = false;
    context_poll = esp_timer_get_time() / 1000000; tick(nullptr);
    assert(protocol_negotiated && !manifest_ready && !busy && cards[0].pixels == pixels && clock_panel == clock);
    context_poll = 0; tick(nullptr); assert(busy && job.kind == CONTEXT && job.version == 3);
    job.context = wanted; job.context.server_time = server_now(); job.result = PHOTO_OK; job.complete = true; busy = false; tick(nullptr);
    assert(!busy && cards[0].pixels == pixels && clock_panel == clock);
    assert(submit(MANIFEST, 0, nullptr)); job.manifest = saved_manifest;
    job.result = PHOTO_OK; job.complete = true; busy = false; tick(nullptr);
    assert(manifest_ready && cards[0].pixels == pixels && clock_panel == clock);
  }
  // Revoking the shown source frame leaves independent information intact.
  current_manifest.frames[0] = current_manifest.frames[1]; current_manifest.count = 1;
  assert(!shown_in_manifest()); last_poll = fake_seconds; last_ready = fake_seconds;
  fake_seconds += 45; tick(nullptr); assert(busy && job.kind == FRAME);
  assert(cards[0].pixels == pixels && cards[0].object == panel && clock_panel == clock);
  complete_frame(); tick(nullptr); fake_seconds += 15; last_ready = fake_seconds; last_poll = fake_seconds; tick(nullptr);
  assert(cards[0].pixels == pixels && cards[0].object == panel && clock_panel == clock);
  if (busy) { job.result = PHOTO_FAILED; job.complete = true; busy = false; tick(nullptr); }
  // OOM during replacement retains the existing unexpired card.
  context_poll = 0; last_poll = fake_seconds; tick(nullptr); assert(busy && job.kind == CONTEXT);
  job.context = wanted; job.context.server_time = server_now(); job.context.blocks[0].id[0] = 'f';
  photo_test_alloc_fail_after = 0; job.result = PHOTO_OK; job.complete = true; busy = false; tick(nullptr);
  assert(cards[0].pixels == pixels && cards[0].object == panel && !busy && card_retry > esp_timer_get_time() / 1000000);
  photo_test_alloc_fail_after = -1;
  // True TTL expires the card but leaves the local clock present.
  cards[0].meta.expires_at = server_now() + 1; ++fake_seconds; tick(nullptr);
  assert(!cards[0].pixels && clock_panel == clock);
  context_poll = 0; last_poll = fake_seconds; tick(nullptr); assert(busy && job.kind == CONTEXT);
  job.context = {}; job.context.version = 3; job.context.server_time = server_now();
  job.result = PHOTO_OK; job.complete = true; busy = false; tick(nullptr);
  assert(!cards[0].pixels && !cards[1].pixels && !clock_panel);
  photo_frame_touch(true); photo_frame_touch(false);
  // Maximum live alpha set plus the largest replacement is below the hard cap.
  manual_requested = true; assert(allocate_buffers()); publish(0, current_manifest.frames[0].id);
  wanted = {}; wanted.version = 3; wanted.count = 2; context_known = true;
  server_anchor = 1793361600; monotonic_anchor = esp_timer_get_time();
  strcpy(wanted.blocks[0].kind, "weather"); wanted.blocks[0].width = 236; wanted.blocks[0].height = 200;
  strcpy(wanted.blocks[1].kind, "event"); wanted.blocks[1].width = 456; wanted.blocks[1].height = 112;
  for (int i = 0; i < 2; ++i) {
    wanted.blocks[i].expires_at = server_now() + 200; cards[i].meta = wanted.blocks[i];
    cards[i].pixels = (uint8_t *)malloc((size_t)wanted.blocks[i].width * wanted.blocks[i].height * 4); assert(cards[i].pixels);
  }
  wanted.blocks[1].id[0] = 'f'; card_retry = 0; context_poll = esp_timer_get_time() / 1000000;
  sim_pixel_peak = 0; schedule_info(); assert(busy && job.kind == CARD);
  assert(sim_pixel_peak == 597376 && sim_pixel_peak <= 768u * 1024u);
  std::printf("v3 alpha peak: %zu bytes\n", sim_pixel_peak);
  // A network handoff cannot free worker-owned staging or accept its late result.
  uint8_t *inflight = job.staging; unsigned old_epoch = job.generation;
  photo_frame_update_network("other-bridge", "new-key"); tick(nullptr);
  assert(busy && job.staging == inflight && epoch.load() != old_epoch && !context_known);
  job.result = PHOTO_OK; job.complete = true; busy = false; tick(nullptr);
  assert(!cards[0].pixels && !cards[1].pixels && !context_known);
  // A changed Bridge must negotiate a manifest before any v3 information request.
  assert(!manifest_ready && !protocol_negotiated && busy && job.kind == MANIFEST);
  job.result = PHOTO_FAILED; job.complete = true; busy = false; tick(nullptr);
  assert(!manifest_ready && !protocol_negotiated && !busy && !context_known);
  retry_after = 0; last_poll = 0; tick(nullptr); assert(busy && job.kind == MANIFEST);
  complete_manifest(); job.manifest.scene_context_version = 3; tick(nullptr);
  assert(manifest_ready && busy && (job.kind == FRAME || job.kind == CONTEXT));
  if (busy) { job.result = PHOTO_CANCELLED; job.complete = true; busy = false; tick(nullptr); }
  photo_frame_touch(true); photo_frame_touch(false);
  std::puts("PASS: idle, gestures, stale completion, reader, failure recovery, "
            "slide scheduling, disabled state, OTA acknowledgement and manual button");
}

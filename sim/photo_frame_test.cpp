#include "../src/photo_frame.cpp"
#include "../src/ui_dashboard.cpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

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
                                  photo_cancel_fn, void *) {
  return PHOTO_FAILED;
}
photo_result photo_fetch_pixels(const char *, const char *,
                                const photo_frame_entry *, uint8_t *,
                                photo_cancel_fn, void *) {
  return PHOTO_FAILED;
}
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
  tick(nullptr);
  assert(overlay && displayed >= 0);
  assert(shown_id[0] == 'a');
  // A press is consumed through release, including a drag and hold.
  pointer(indev, true, 10, 10);
  assert(!overlay);
  pointer(indev, true, 80, 80);
  pointer(indev, false, 80, 80);
  assert(clicks == 0);
  pointer(indev, true, 10, 10);
  pointer(indev, false, 10, 10);
  assert(clicks == 1);
  // An unfinished transfer from the previous idle generation cannot publish.
  fake_seconds = 600;
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
  open_reader("fixture", "fixture", "fixture", nullptr);
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
  // Stale source must exit without immediately redisplaying an old frame.
  last_ready = fake_seconds - 900;
  last_poll = fake_seconds;
  busy = false;
  job.complete = false;
  tick(nullptr);
  assert(!overlay);
  assert(!busy.load());
  assert(!manifest_ready);
  // A previously ready manifest cannot start a first frame after expiry.
  manifest_ready = true;
  last_ready = fake_seconds - 900;
  last_poll = fake_seconds;
  retry_after = 0;
  last_touch = fake_seconds - 300;
  tick(nullptr);
  assert(!manifest_ready);
  assert(!busy.load());
  assert(!overlay);
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
  // The actual Home button starts without waiting for the idle threshold.
  fake_seconds = 5000;
  last_touch = fake_seconds;
  last_poll = fake_seconds;
  retry_after = fake_seconds + 300;
  lv_tileview_set_tile(tileview, nav_tiles[2], LV_ANIM_OFF);
  lv_obj_update_layout(lv_screen_active());
  pointer(indev, true, 740, 20);
  pointer(indev, false, 740, 20);
  assert(busy.load() && job.kind == MANIFEST);
  assert(manual_requested);
  assert(strstr(lv_label_get_text(home_photo_status), "Получаем"));
  complete_manifest();
  tick(nullptr);
  assert(busy.load() && job.kind == FRAME);
  complete_frame();
  tick(nullptr);
  assert(overlay);
  assert(lv_tileview_get_tile_active(tileview) == nav_tiles[2]);
  // Dismiss over the launch button: the same gesture cannot relaunch it.
  pointer(indev, true, 740, 20);
  pointer(indev, false, 740, 20);
  assert(!overlay && !manual_requested && !busy.load());
  // A failed manual attempt keeps its diagnostic visible on the Home page.
  pointer(indev, true, 740, 20);
  pointer(indev, false, 740, 20);
  assert(busy.load() && job.kind == MANIFEST);
  job.result = PHOTO_FAILED;
  job.complete = true;
  busy = false;
  tick(nullptr);
  assert(!overlay);
  assert(strstr(lv_label_get_text(home_photo_status), "список"));
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
  assert(strstr(lv_label_get_text(home_photo_status), "выключена"));
  paused = true;
  assert(!photo_frame_start_now());
  assert(strstr(lv_label_get_text(home_photo_status), "обновления"));
  paused = false;
  std::puts("PASS: idle, gestures, stale completion, reader, failure recovery, "
            "slide scheduling, disabled state, OTA acknowledgement and manual button");
}

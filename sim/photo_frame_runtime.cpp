#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "photo_frame_client.h"
#include "cJSON.h"
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <ctime>
#include <cstdlib>
#include <cstdio>
struct FakeTask {
  std::mutex mutex;
  std::condition_variable wake;
  unsigned notifications = 0;
};
static thread_local FakeTask *current = nullptr;
SemaphoreHandle_t xSemaphoreCreateMutex() { return new std::mutex; }
int xSemaphoreTake(SemaphoreHandle_t h, unsigned) {
  static_cast<std::mutex *>(h)->lock();
  return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t h) {
  static_cast<std::mutex *>(h)->unlock();
  return pdTRUE;
}
int xTaskCreatePinnedToCore(void (*fn)(void *), const char *, unsigned,
                            void *arg, unsigned, TaskHandle_t *handle, int) {
  auto *t = new FakeTask;
  *handle = t;
  std::thread([t, fn, arg] {
    current = t;
    fn(arg);
  }).detach();
  return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t h) {
  auto *t = static_cast<FakeTask *>(h);
  std::lock_guard<std::mutex> lock(t->mutex);
  ++t->notifications;
  t->wake.notify_one();
}
unsigned ulTaskNotifyTake(int, unsigned) {
  std::unique_lock<std::mutex> lock(current->mutex);
  current->wake.wait(lock, [] { return current->notifications > 0; });
  auto n = current->notifications;
  current->notifications = 0;
  return n;
}
void vTaskDelay(unsigned ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
bool lvgl_port_lock(int) { return true; }
void lvgl_port_unlock() {}
photo_result photo_fetch_manifest(const char *, const char *, photo_manifest *m,
                                  photo_cancel_fn cancel, void *arg, photo_diagnostic *) {
  if (cancel(arg))
    return PHOTO_CANCELLED;
  *m = {};
  strcpy(m->state, "ready");
  strcpy(m->generation, "fixture");
  m->idle = 300;
  m->interval = 60;
  m->stale = 900;
  if (std::getenv("HOMEDASH_SIM_ADAPTIVE_JSON")) m->overlay_context_version = 2;
  if (std::getenv("HOMEDASH_SIM_UNIFIED_JSON")) { m->overlay_context_version = 2; m->scene_context_version = 3; }
  m->count = 2;
  memset(m->frames[0].id, 'a', 64);
  memset(m->frames[1].id, 'b', 64);
  return PHOTO_OK;
}
photo_result photo_fetch_pixels(const char *, const char *,
                                const photo_frame_entry *e, uint8_t *data,
                                photo_cancel_fn cancel, void *arg, photo_diagnostic *) {
  const char *path = std::getenv("HOMEDASH_SIM_PHOTO_RGB565");
  if (path) {
    FILE *file = std::fopen(path, "rb"); if (!file) return PHOTO_FAILED;
    size_t got = std::fread(data, 1, PHOTO_FRAME_BYTES, file); int extra = std::fgetc(file); std::fclose(file);
    return got == PHOTO_FRAME_BYTES && extra == EOF && !cancel(arg) ? PHOTO_OK : PHOTO_FAILED;
  }
  for (unsigned y = 0; y < 600; y++) {
    if (cancel(arg))
      return PHOTO_CANCELLED;
    for (unsigned x = 0; x < 1024; x++) {
      unsigned v = e->id[0] == 'a' ? ((x / 64 % 2) ? 0xf800 : 0x07e0)
                                   : ((y / 64 % 2) ? 0x001f : 0xffe0);
      unsigned p = (y * 1024 + x) * 2;
      data[p] = v;
      data[p + 1] = v >> 8;
    }
  }
  return PHOTO_OK;
}
photo_result photo_fetch_context(const char *, const char *, photo_context *out, photo_cancel_fn cancel, void *arg, photo_diagnostic *, int version, const char *frame_id) {
  if (cancel(arg)) return PHOTO_CANCELLED;
  *out = {}; out->server_time = time(nullptr); out->clock_enabled = true;
  const char *locale = std::getenv("HOMEDASH_SIM_PHOTO_LOCALE");
  const char *format = std::getenv("HOMEDASH_SIM_PHOTO_CLOCK_FORMAT");
  snprintf(out->locale, sizeof(out->locale), "%s", locale ? locale : "en");
  strcpy(out->timezone, "Europe/Belgrade"); snprintf(out->clock_format, sizeof(out->clock_format), "%s", format ? format : "24h");
  if (version == 2 || version == 3) {
    const char *path = std::getenv(version == 3 ? "HOMEDASH_SIM_UNIFIED_JSON" : "HOMEDASH_SIM_ADAPTIVE_JSON");
    FILE *file = path ? std::fopen(path, "rb") : nullptr; if (!file) return PHOTO_FAILED;
    char json[4096]; size_t len = std::fread(json, 1, sizeof(json)-1, file); std::fclose(file); json[len] = 0;
    cJSON *root = cJSON_Parse(json); if (!root) return PHOTO_FAILED;
    out->version = version; snprintf(out->frame_id, sizeof(out->frame_id), "%s", frame_id);
    const char *kinds[] = {"weather", "event", "clock"};
    for (int k = 0; k < (version == 3 ? 2 : 3); ++k) {
      const char *kind = kinds[k];
      cJSON *entry = cJSON_GetObjectItemCaseSensitive(root, kind);
      if (!entry) { cJSON_Delete(root); return PHOTO_FAILED; }
      if (!strcmp(kind, "clock")) {
        cJSON *tone = cJSON_GetObjectItemCaseSensitive(entry, "tone");
        if (!cJSON_IsString(tone)) { cJSON_Delete(root); return PHOTO_FAILED; }
        snprintf(out->tone, sizeof(out->tone), "%s", tone->valuestring);
        out->backdrop = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(entry, "backdrop"));
        if (!out->backdrop) continue;
      }
      photo_overlay_block &b = out->blocks[out->count++]; snprintf(b.kind, sizeof(b.kind), "%s", kind);
      memset(b.id, 'a' + out->count, 64); memset(b.sha256, 'd' + out->count, 64);
      b.x = cJSON_GetObjectItemCaseSensitive(entry, "x")->valueint; b.y = cJSON_GetObjectItemCaseSensitive(entry, "y")->valueint;
      b.width = cJSON_GetObjectItemCaseSensitive(entry, "width")->valueint; b.height = cJSON_GetObjectItemCaseSensitive(entry, "height")->valueint;
      b.expires_at = out->server_time + 300;
    }
    if (version == 3) { strcpy(out->tone, "light"); out->backdrop = true; }
    cJSON_Delete(root); return PHOTO_OK;
  }
  out->count = 2;
  strcpy(out->blocks[0].kind, "weather"); memset(out->blocks[0].id, 'a', 64);
  out->blocks[0].x = 752; out->blocks[0].y = 36; out->blocks[0].width = 236; out->blocks[0].height = 200; out->blocks[0].expires_at = out->server_time + 1800;
  strcpy(out->blocks[1].kind, "event"); memset(out->blocks[1].id, 'b', 64);
  out->blocks[1].x = 506; out->blocks[1].y = 450; out->blocks[1].width = 480; out->blocks[1].height = 112; out->blocks[1].expires_at = out->server_time + 300;
  return PHOTO_OK;
}
photo_result photo_fetch_overlay(const char *, const char *, const photo_overlay_block *block, uint8_t *data, photo_cancel_fn cancel, void *arg, photo_diagnostic *, int version) {
  if (cancel(arg)) return PHOTO_CANCELLED;
  const char *path = std::getenv(!strcmp(block->kind, "weather") ? "HOMEDASH_SIM_WEATHER_RGB565" : !strcmp(block->kind, "event") ? "HOMEDASH_SIM_EVENT_RGB565" : "HOMEDASH_SIM_CLOCK_RGB565");
  if (path) {
    FILE *file = std::fopen(path, "rb"); if (!file) return PHOTO_FAILED;
    size_t bytes = (size_t)block->width * block->height * (version == 3 ? 4 : 2);
    size_t got = std::fread(data, 1, bytes, file); int extra = std::fgetc(file); std::fclose(file);
    return got == bytes && extra == EOF ? PHOTO_OK : PHOTO_FAILED;
  }
  for (int i = 0; i < block->width * block->height; ++i) { data[2*i] = 0x84; data[2*i+1] = 0x10; }
  return PHOTO_OK;
}

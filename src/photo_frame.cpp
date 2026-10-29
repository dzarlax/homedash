#include "photo_frame.h"
#include "photo_frame_client.h"
#include "ui_dashboard.h"
#ifndef SIMULATOR
#include "lvgl_port.h"
#else
bool lvgl_port_lock(int);
void lvgl_port_unlock(void);
#endif
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <atomic>
#include <stdio.h>
#include <string.h>

namespace {
enum job_kind { NONE, MANIFEST, FRAME };
struct job_data {
  job_kind kind;
  unsigned generation;
  int buffer;
  photo_frame_entry frame;
  photo_manifest manifest;
  photo_result result;
  photo_diagnostic diagnostic;
  bool complete;
};
static job_data job = {};
static SemaphoreHandle_t mutex;
static std::atomic<TaskHandle_t> worker{nullptr};
static std::atomic<unsigned> epoch{1};
static std::atomic<bool> paused{false}, busy{false};
static char bridge_url[128], bridge_key[64];
static char pending_bridge_url[128], pending_bridge_key[64];
static bool network_update_pending = false;
static uint8_t *buffers[2] = {};
static lv_image_dsc_t descriptor[2] = {};
static lv_obj_t *overlay = nullptr, *image = nullptr;
static int displayed = -1;
static bool gesture = false, physical_down = false;
static int64_t last_touch = 0, last_poll = 0, last_ready = 0, last_slide = 0,
               retry_after = 0;
static int retry_step = 0, idle_seconds = 300, interval_seconds = 60,
           stale_seconds = 900;
static bool web_photos_enabled = true;
static bool web_timing_enabled = false;
static char shown_id[65] = {};
static photo_manifest current_manifest = {};
static bool manifest_ready = false;
// UI-owned override; cleared by the next physical touch or OTA handoff.
static bool manual_requested = false;
#ifdef SIMULATOR
static bool sim_accelerated = false;
#endif
static int64_t now() {
  int64_t seconds = esp_timer_get_time() / 1000000;
#ifdef SIMULATOR
  if (sim_accelerated)
    seconds *= 30;
#endif
  return seconds;
}
static bool cancelled(void *p) {
  return paused.load() || epoch.load() != *static_cast<unsigned *>(p);
}
static void detach() {
  if (overlay) {
    lv_obj_delete(overlay);
    overlay = nullptr;
    image = nullptr;
  }
  if (displayed >= 0) {
    lv_image_cache_drop(&descriptor[displayed]);
    displayed = -1;
  }
  shown_id[0] = 0;
}
static void free_buffers() {
  detach();
  for (auto &b : buffers) {
    free(b);
    b = nullptr;
  }
}
static bool allocate_buffers() {
  if (buffers[0] && buffers[1])
    return true;
  buffers[0] = (uint8_t *)heap_caps_malloc(PHOTO_FRAME_BYTES,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  buffers[1] = (uint8_t *)heap_caps_malloc(PHOTO_FRAME_BYTES,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buffers[0] || !buffers[1]) {
    free_buffers();
    retry_after = now() + 300;
    ui_dashboard_update_photo_status("Недостаточно памяти для фото");
    return false;
  }
  return true;
}
static void publish(int index, const char *id) {
  if (!overlay) {
    overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, 1024, 600);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    image = lv_image_create(overlay);
  }
  lv_image_dsc_t &d = descriptor[index];
  memset(&d, 0, sizeof(d));
  d.header.magic = LV_IMAGE_HEADER_MAGIC;
  d.header.cf = LV_COLOR_FORMAT_RGB565;
  d.header.w = 1024;
  d.header.h = 600;
  d.header.stride = 2048;
  d.data_size = PHOTO_FRAME_BYTES;
  d.data = buffers[index];
  lv_image_cache_drop(&d);
  lv_image_set_src(image, &d);
  lv_obj_invalidate(image);
  if (displayed >= 0 && displayed != index)
    lv_image_cache_drop(&descriptor[displayed]);
  displayed = index;
  strncpy(shown_id, id, 64);
  shown_id[64] = 0;
  last_slide = now();
  ui_dashboard_update_photo_status("Фото открыто. Касание: выход");
}
static void worker_task(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    xSemaphoreTake(mutex, portMAX_DELAY);
    job_data local = job;
    xSemaphoreGive(mutex);
    photo_result result = PHOTO_CANCELLED;
    if (!cancelled(&local.generation)) {
      if (local.kind == MANIFEST)
        result = photo_fetch_manifest(bridge_url, bridge_key, &local.manifest,
                                      cancelled, &local.generation, &local.diagnostic);
      else if (local.kind == FRAME)
        result = photo_fetch_pixels(bridge_url, bridge_key, &local.frame,
                                    buffers[local.buffer], cancelled,
                                    &local.generation, &local.diagnostic);
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    local.result = result;
    local.complete = true;
    job = local;
    busy.store(false);
    xSemaphoreGive(mutex);
  }
}
static bool submit(job_kind kind, int index, const photo_frame_entry *entry) {
  TaskHandle_t target = worker.load(std::memory_order_acquire);
  if (!mutex || !target || paused.load() || busy.load())
    return false;
  xSemaphoreTake(mutex, portMAX_DELAY);
  if (job.complete || busy.load() || paused.load()) {
    xSemaphoreGive(mutex);
    return false;
  }
  job = {};
  job.kind = kind;
  job.generation = epoch.load();
  job.buffer = index;
  if (entry)
    job.frame = *entry;
  busy.store(true);
  xSemaphoreGive(mutex);
  xTaskNotifyGive(target);
  ui_dashboard_update_photo_status(kind == MANIFEST ? "Получаем список фото..."
                                                   : "Загружаем фото...");
  return true;
}
static void retry() {
  static const int delays[] = {30, 60, 120, 300};
  retry_after = now() + delays[retry_step < 4 ? retry_step : 3];
  if (retry_step < 3)
    retry_step++;
}
static void tick(lv_timer_t *) {
  if (!mutex || paused.load())
    return;
  if (!busy.load()) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (network_update_pending) {
      strncpy(bridge_url, pending_bridge_url, sizeof(bridge_url) - 1);
      strncpy(bridge_key, pending_bridge_key, sizeof(bridge_key) - 1);
      bridge_url[sizeof(bridge_url) - 1] = 0;
      bridge_key[sizeof(bridge_key) - 1] = 0;
      network_update_pending = false;
      manifest_ready = false;
      last_ready = 0;
      last_poll = 0;
      retry_after = 0;
    }
    xSemaphoreGive(mutex);
  }
  if (!web_photos_enabled) {
    manual_requested = false;
    if (overlay || buffers[0] || buffers[1]) free_buffers();
    manifest_ready = false;
    return;
  }
  job_data result = {};
  bool completed = false;
  xSemaphoreTake(mutex, portMAX_DELAY);
  if (job.complete) {
    result = job;
    job.complete = false;
    completed = true;
  }
  xSemaphoreGive(mutex);
  int64_t t = now();
  bool eligible = !physical_down && !ui_dashboard_reader_open() &&
                  (manual_requested || t - last_touch >= idle_seconds);
  if (completed && result.generation == epoch.load()) {
    if (result.kind == MANIFEST && result.result == PHOTO_OK) {
      photo_manifest &m = result.manifest;
      if (!web_timing_enabled) {
        idle_seconds = m.idle;
        interval_seconds = m.interval;
      }
      stale_seconds = m.stale;
      eligible = !physical_down && !ui_dashboard_reader_open() &&
                 (manual_requested || t - last_touch >= idle_seconds);
      if (!strcmp(m.state, "disabled") || !strcmp(m.state, "empty")) {
        detach();
        free_buffers();
        last_ready = 0;
        retry_step = 0;
        manifest_ready = false;
        manual_requested = false;
        ui_dashboard_update_photo_status(!strcmp(m.state, "disabled")
                                             ? "Фоторамка выключена на мосту"
                                             : "В подборке нет фотографий");
      } else if (!strcmp(m.state, "ready")) {
        last_ready = t;
        retry_step = 0;
        retry_after = 0;
        current_manifest = m;
        manifest_ready = true;
      } else {
        manifest_ready = false;
        ui_dashboard_update_photo_status(!strcmp(m.state, "warming")
                                             ? "Мост готовит фотографии..."
                                             : "Фотографии пока недоступны");
        retry();
      }
    } else if (result.kind == FRAME && result.result == PHOTO_OK && eligible) {
      publish(result.buffer, result.frame.id);
      retry_step = 0;
      retry_after = 0;
    } else if (result.result == PHOTO_NOT_FOUND && result.kind == FRAME) {
      last_poll = 0;
      retry_after = 0;
    } else if (result.result != PHOTO_CANCELLED) {
      char message[144];
      if (result.kind == FRAME)
        snprintf(message, sizeof(message), "Фото: %s %u/%u H%d E%d",
                 result.diagnostic.stage, (unsigned)result.diagnostic.received,
                 PHOTO_FRAME_BYTES, result.diagnostic.http_status,
                 result.diagnostic.transport_error);
      else
        snprintf(message, sizeof(message), "Список фото: %s / HTTP %d / err %d",
                 result.diagnostic.stage, result.diagnostic.http_status,
                 result.diagnostic.transport_error);
      ui_dashboard_update_photo_status(message);
      retry();
    }
  }
  if (last_ready && t - last_ready >= stale_seconds) {
    const bool was_visible = overlay != nullptr;
    detach();
    epoch.fetch_add(1);
    manifest_ready = false;
    last_ready = 0;
    manual_requested = false;
    ui_dashboard_update_photo_status("Нет связи с источником фото");
    if (was_visible) {
      last_touch = t;
      return;
    }
  }
  if (eligible && !busy.load() && t >= retry_after &&
      (last_poll == 0 || t - last_poll >= 60)) {
    if (submit(MANIFEST, 0, nullptr))
      last_poll = t;
  }
  if (eligible && manifest_ready && !busy.load() && t >= retry_after &&
      current_manifest.count > 0 &&
      (!overlay || t - last_slide >= interval_seconds)) {
    int next_frame = 0;
    if (overlay) {
      int current = -1;
      for (int i = 0; i < current_manifest.count; i++)
        if (!strcmp(shown_id, current_manifest.frames[i].id)) {
          current = i;
          break;
        }
      if (current >= 0)
        next_frame = (current + 1) % current_manifest.count;
    }
    const photo_frame_entry *entry = &current_manifest.frames[next_frame];
    if (strcmp(shown_id, entry->id) && allocate_buffers())
      submit(FRAME, displayed == 0 ? 1 : 0, entry);
  }
}
} // namespace
void photo_frame_ui_init() {
  mutex = xSemaphoreCreateMutex();
  last_touch = now();
  lv_timer_create(tick, 1000, nullptr);
}
void photo_frame_network_init(const char *url, const char *key) {
  if (!mutex)
    return;
  strncpy(bridge_url, url, sizeof(bridge_url) - 1);
  strncpy(bridge_key, key, sizeof(bridge_key) - 1);
  TaskHandle_t handle = nullptr;
  if (xTaskCreatePinnedToCore(worker_task, "photo", 12 * 1024, nullptr, 0,
                              &handle, 0) == pdPASS)
    worker.store(handle, std::memory_order_release);
}
void photo_frame_update_network(const char *url, const char *key) {
  if (!mutex || !url || !key || strlen(url) >= sizeof(bridge_url) ||
      strlen(key) >= sizeof(bridge_key)) return;
  xSemaphoreTake(mutex, portMAX_DELAY);
  epoch.fetch_add(1);
  strncpy(pending_bridge_url, url, sizeof(pending_bridge_url) - 1);
  pending_bridge_url[sizeof(pending_bridge_url) - 1] = 0;
  strncpy(pending_bridge_key, key, sizeof(pending_bridge_key) - 1);
  pending_bridge_key[sizeof(pending_bridge_key) - 1] = 0;
  network_update_pending = true;
  xSemaphoreGive(mutex);
}
void photo_frame_apply_web_preferences(bool enabled, int idle, int interval) {
  if (idle < 30 || idle > 3600 || interval < 15 || interval > 3600) return;
  if (!lvgl_port_lock(-1)) return;
  web_photos_enabled = enabled;
  web_timing_enabled = true;
  idle_seconds = idle;
  interval_seconds = interval;
  if (!enabled) {
    manual_requested = false;
    epoch.fetch_add(1);
  }
  lvgl_port_unlock();
}
bool photo_frame_start_now() {
  if (!mutex || !worker.load(std::memory_order_acquire)) {
    ui_dashboard_update_photo_status("Фоторамка ещё не готова");
    return false;
  }
  if (paused.load()) {
    ui_dashboard_update_photo_status("Сначала дождитесь обновления");
    return false;
  }
  if (!web_photos_enabled) {
    ui_dashboard_update_photo_status("Фоторамка выключена в настройках");
    return false;
  }
  epoch.fetch_add(1);
  detach();
  manual_requested = true;
  manifest_ready = false;
  last_ready = 0;
  last_poll = 0;
  last_touch = now();
  retry_after = 0;
  retry_step = 0;
  ui_dashboard_update_photo_status("Запускаем фоторамку...");
  tick(nullptr);
  return true;
}
bool photo_frame_touch(bool pressed) {
  int64_t t = now();
  if (pressed) {
    if (!physical_down) {
      epoch.fetch_add(1);
      if (manual_requested || overlay)
        ui_dashboard_update_photo_status("Авто: после простоя без касаний");
      manual_requested = false;
    }
    physical_down = true;
    last_touch = t;
    if (overlay) {
      gesture = true;
      detach();
    }
    return gesture;
  }
  physical_down = false;
  bool consumed = gesture;
  gesture = false;
  return consumed;
}
void photo_frame_filter_touch(bool pressed, uint16_t x, uint16_t y,
                              lv_indev_data_t *data) {
  if (photo_frame_touch(pressed)) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  if (pressed) {
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else
    data->state = LV_INDEV_STATE_RELEASED;
}
void photo_frame_reader_closed() {
  manual_requested = false;
  last_touch = now();
  epoch.fetch_add(1);
}
bool photo_frame_pause_for_ota() {
  if (!mutex)
    return true;
  paused.store(true);
  epoch.fetch_add(1);
  int64_t deadline = esp_timer_get_time() + 3000000;
  while (busy.load() && esp_timer_get_time() < deadline)
    vTaskDelay(pdMS_TO_TICKS(20));
  if (busy.load()) {
    paused.store(false);
    return false;
  }
  if (lvgl_port_lock(3000)) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    job.complete = false;
    xSemaphoreGive(mutex);
    free_buffers();
    manifest_ready = false;
    manual_requested = false;
    last_ready = 0;
    last_poll = 0;
    last_touch = now();
    lvgl_port_unlock();
    return true;
  }
  paused.store(false);
  return false;
}
void photo_frame_resume_after_ota() { paused.store(false); }

#ifdef SIMULATOR
void photo_frame_sim_accelerate() { sim_accelerated = true; }
#endif

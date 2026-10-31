#include "photo_frame.h"
#include "photo_frame_client.h"
#include "ui_dashboard.h"
#include "ui_strings.h"
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
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace {
enum job_kind { NONE, MANIFEST, FRAME, CONTEXT, CARD };
struct job_data {
  job_kind kind;
  unsigned generation;
  int64_t request_start_us;
  int buffer, version;
  char bound_frame[65];
  photo_frame_entry frame;
  photo_manifest manifest;
  photo_context context;
  photo_overlay_block card;
  uint8_t *staging;
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
static bool touch_started_in_photo = false, controls_visible = false;
static bool slideshow_paused = false, info_hidden = false, load_error = false;
static bool retry_requested = false, stale_refresh_pending = false;
static int touch_start_x = 0, touch_start_y = 0;
static int touch_last_x = 0, touch_last_y = 0;
static int touch_start_action = -1;
static bool touch_hold_opened = false, touch_hold_cancelled = false;
static int64_t touch_started_us = 0;
static constexpr int TOUCH_TAP_SLOP = 16;
static constexpr int64_t TOUCH_HOLD_US = 700000;
static int64_t controls_until = 0;
static lv_obj_t *control_bar = nullptr, *control_status = nullptr;
static lv_obj_t *control_buttons[6] = {}, *control_labels[6] = {};
static int64_t now();
static int64_t last_touch = 0, last_poll = 0, last_ready = 0, last_slide = 0,
               retry_after = 0;
static int retry_step = 0, idle_seconds = 300, interval_seconds = 60,
           stale_seconds = 900;
static bool web_photos_enabled = true;
static bool web_timing_enabled = false;
static char shown_id[65] = {};
static photo_manifest current_manifest = {};
static bool manifest_ready = false, protocol_negotiated = false;
static bool shown_in_manifest() {
  if (!manifest_ready || !shown_id[0]) return false;
  for (int i = 0; i < current_manifest.count; ++i)
    if (!strcmp(shown_id, current_manifest.frames[i].id)) return true;
  return false;
}
struct card_state { photo_overlay_block meta; uint8_t *pixels; lv_image_dsc_t desc; lv_obj_t *object; };
static card_state cards[3] = {};
#ifdef SIMULATOR
static size_t sim_pixel_peak = 0;
#endif
static photo_context wanted = {};
struct pending_photo { bool ready; unsigned generation; int buffer; char id[65]; };
static pending_photo pending = {};
static int manual_target = -1;
static bool manual_target_pending = false;
static char manual_target_id[65] = {};
static void discard_pending() { pending = {}; }
static bool frame_in_manifest(const char *id) {
  if (!manifest_ready) return false;
  for (int i = 0; i < current_manifest.count; ++i) if (!strcmp(id, current_manifest.frames[i].id)) return true;
  return false;
}
static int64_t server_anchor = 0, monotonic_anchor = 0, context_poll = 0, card_retry = 0;
static bool context_known = false;
static char confirmed_timezone[40] = {}, confirmed_posix_timezone[48] = {};
static lv_obj_t *clock_label = nullptr, *date_label = nullptr, *clock_panel = nullptr;
static lv_obj_t *clock_shadow[4] = {}, *date_shadow[4] = {};
static int last_minute = -1;
static const char *months[][12] = {{"января","февраля","марта","апреля","мая","июня","июля","августа","сентября","октября","ноября","декабря"},{"January","February","March","April","May","June","July","August","September","October","November","December"},{"januar","februar","mart","april","maj","jun","jul","avgust","septembar","oktobar","novembar","decembar"}};
static const char *days[][7] = {{"воскресенье","понедельник","вторник","среда","четверг","пятница","суббота"},{"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"},{"nedelja","ponedeljak","utorak","sreda","četvrtak","petak","subota"}};
static int language_index(const char *locale) { return !strcmp(locale,"en") ? 1 : !strcmp(locale,"sr-Latn") ? 2 : 0; }
static void format_date(char *buf, size_t cap, int language, int day, int weekday, int month) {
  snprintf(buf, cap, "%s, %d %s", days[language][weekday], day, months[language][month]);
}
static void format_time(char *buf, size_t cap, int hour, int minute, bool twelve) {
  if (twelve) snprintf(buf, cap, "%d:%02d %s", hour % 12 ? hour % 12 : 12, minute, hour >= 12 ? "PM" : "AM");
  else snprintf(buf, cap, "%02d:%02d", hour, minute);
}
static void measure_clock_widths(int widths[6]) {
  static int cached[6] = {};
  if (cached[0]) { memcpy(widths, cached, sizeof(cached)); return; }
  char buf[128]; lv_point_t size;
  for (int lang = 0; lang < 3; ++lang) {
    int date_width = 0;
    for (int day = 1; day <= 31; ++day) for (int weekday = 0; weekday < 7; ++weekday) for (int month = 0; month < 12; ++month) {
      format_date(buf, sizeof(buf), lang, day, weekday, month);
      lv_text_get_size(&size, buf, &font_montserrat_16_cyr, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
      if (size.x > date_width) date_width = size.x;
    }
    for (int twelve = 0; twelve < 2; ++twelve) {
      int width = date_width;
      for (int minute = 0; minute < 1440; ++minute) {
        format_time(buf, sizeof(buf), minute / 60, minute % 60, twelve);
        lv_text_get_size(&size, buf, &font_photo_clock_80, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x > width) width = size.x;
      }
      width += 34;
      widths[lang * 2 + twelve] = width < 144 ? 144 : width > 480 ? 480 : width;
    }
  }
  memcpy(cached, widths, sizeof(cached));
}
static int64_t server_now() { return server_anchor + (esp_timer_get_time() - monotonic_anchor) / 1000000; }
static void clear_card(int i) {
  if (cards[i].object) lv_obj_delete(cards[i].object);
  if (cards[i].pixels) { lv_image_cache_drop(&cards[i].desc); free(cards[i].pixels); }
  cards[i] = {};
}
static int slot(const char *kind) { return !strcmp(kind, "weather") ? 0 : !strcmp(kind, "event") ? 1 : 2; }
static void clear_context() {
  for (int i = 0; i < 3; ++i) clear_card(i);
  if (clock_label) lv_obj_delete(clock_label);
  if (date_label) lv_obj_delete(date_label);
  for (int i = 0; i < 4; ++i) {
    if (clock_shadow[i]) lv_obj_delete(clock_shadow[i]);
    if (date_shadow[i]) lv_obj_delete(date_shadow[i]);
    clock_shadow[i] = date_shadow[i] = nullptr;
  }
  if (clock_panel) lv_obj_delete(clock_panel);
  clock_panel = nullptr;
  clock_label = date_label = nullptr; last_minute = -1; context_known = false; wanted = {};
}
static void set_info_hidden(bool hidden) {
  info_hidden = hidden;
  for (int i = 0; i < 3; ++i) if (cards[i].object) {
    if (hidden) lv_obj_add_flag(cards[i].object, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(cards[i].object, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_t *objects[] = {clock_panel, clock_label, date_label,
                         clock_shadow[0], clock_shadow[1], clock_shadow[2], clock_shadow[3],
                         date_shadow[0], date_shadow[1], date_shadow[2], date_shadow[3]};
  for (lv_obj_t *object : objects) if (object) {
    if (hidden) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
  }
}
static void clear_controls() {
  if (control_bar) lv_obj_delete(control_bar);
  control_bar = control_status = nullptr;
  for (int i = 0; i < 6; ++i) control_buttons[i] = control_labels[i] = nullptr;
  controls_visible = false;
  controls_until = 0;
}
static void controls_raise() {
  if (!controls_visible || !control_bar) return;
  lv_obj_move_foreground(control_bar);
}
static const char *control_caption(int action) {
  switch (action) {
  case 0: return ui_text("Предыдущая");
  case 1: return ui_text("Следующая");
  case 2: return ui_text(slideshow_paused ? "Продолжить" : "Пауза");
  case 3: return ui_text(info_hidden ? "Показать данные" : "Скрыть данные");
  case 4: return ui_text("Повторить");
  default: return ui_text("Выйти");
  }
}
static void update_controls() {
  if (!control_bar) return;
  const int count = load_error ? 6 : 5;
  const int bar_width = 800, gap = 6, margin = 14;
  const int button_width = (bar_width - 2 * margin - gap * (count - 1)) / count;
  const int button_y = load_error ? 32 : 18;
  for (int i = 0; i < 6; ++i) {
    bool visible = i < count;
    if (visible) lv_obj_clear_flag(control_buttons[i], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(control_buttons[i], LV_OBJ_FLAG_HIDDEN);
    if (!visible) continue;
    int action = (i == count - 1) ? 5 : i;
    lv_obj_set_pos(control_buttons[i], margin + i * (button_width + gap), button_y);
    lv_obj_set_size(control_buttons[i], button_width, 48);
    lv_label_set_text(control_labels[i], control_caption(action));
    lv_obj_center(control_labels[i]);
  }
  if (load_error) {
    lv_obj_clear_flag(control_status, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(control_status, ui_text("Не удалось загрузить фото"));
    lv_obj_set_width(control_status, bar_width - 2 * margin);
    lv_obj_set_style_text_align(control_status, LV_TEXT_ALIGN_CENTER, 0);
  } else lv_obj_add_flag(control_status, LV_OBJ_FLAG_HIDDEN);
  controls_raise();
}
static void show_controls() {
  if (!overlay) return;
  if (!control_bar) {
    control_bar = lv_obj_create(overlay);
    lv_obj_remove_style_all(control_bar);
    lv_obj_set_pos(control_bar, 112, 258);
    lv_obj_set_size(control_bar, 800, 84);
    lv_obj_set_style_bg_color(control_bar, lv_color_hex(0x080b10), 0);
    lv_obj_set_style_bg_opa(control_bar, 235, 0);
    lv_obj_set_style_radius(control_bar, 14, 0);
    lv_obj_set_style_border_width(control_bar, 1, 0);
    lv_obj_set_style_border_color(control_bar, lv_color_hex(0x71839a), 0);
    lv_obj_set_style_border_opa(control_bar, 120, 0);
    lv_obj_clear_flag(control_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(control_bar, LV_OBJ_FLAG_CLICKABLE);
    control_status = lv_label_create(control_bar);
    lv_obj_set_pos(control_status, 14, 2);
    lv_obj_set_style_text_color(control_status, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(control_status, &font_montserrat_16_cyr, 0);
    for (int i = 0; i < 6; ++i) {
      control_buttons[i] = lv_obj_create(control_bar);
      lv_obj_remove_style_all(control_buttons[i]);
      lv_obj_set_style_bg_color(control_buttons[i], lv_color_hex(0x263343), 0);
      lv_obj_set_style_bg_opa(control_buttons[i], 255, 0);
      lv_obj_set_style_border_color(control_buttons[i], lv_color_hex(0x71839a), 0);
      lv_obj_set_style_border_width(control_buttons[i], 1, 0);
      lv_obj_set_style_radius(control_buttons[i], 8, 0);
      lv_obj_clear_flag(control_buttons[i], LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_clear_flag(control_buttons[i], LV_OBJ_FLAG_CLICKABLE);
      control_labels[i] = lv_label_create(control_buttons[i]);
      lv_obj_set_style_text_color(control_labels[i], lv_color_hex(0xffffff), 0);
      lv_obj_set_style_text_font(control_labels[i], &font_montserrat_16_cyr, 0);
      lv_obj_set_style_text_align(control_labels[i], LV_TEXT_ALIGN_CENTER, 0);
    }
  }
  controls_visible = true;
  controls_until = now() + 5;
  update_controls();
}
static void render_clock() {
  if (wanted.version == 3 && !wanted.clock_enabled && clock_panel) { lv_obj_delete(clock_panel); clock_panel = nullptr; }
  if (!overlay || !context_known || !wanted.clock_enabled || !ui_dashboard_timezone_matches(wanted.timezone, confirmed_posix_timezone) || strcmp(confirmed_timezone, wanted.timezone)) {
    if (clock_panel) lv_obj_add_flag(clock_panel, LV_OBJ_FLAG_HIDDEN);
    if (clock_label) lv_obj_add_flag(clock_label, LV_OBJ_FLAG_HIDDEN);
    if (date_label) lv_obj_add_flag(date_label, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 4; ++i) {
      if (clock_shadow[i]) lv_obj_add_flag(clock_shadow[i], LV_OBJ_FLAG_HIDDEN);
      if (date_shadow[i]) lv_obj_add_flag(date_shadow[i], LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }
  time_t wall = time(nullptr);
#ifdef SIMULATOR
  const char *fixed_clock = getenv("HOMEDASH_SIM_FIXED_CLOCK");
  if (fixed_clock) wall = (time_t)strtoll(fixed_clock, nullptr, 10);
#endif
  struct tm t = {};
  if (wall < 1577836800 || !localtime_r(&wall, &t)) {
    if (clock_panel) lv_obj_add_flag(clock_panel, LV_OBJ_FLAG_HIDDEN);
    if (clock_label) lv_obj_add_flag(clock_label, LV_OBJ_FLAG_HIDDEN);
    if (date_label) lv_obj_add_flag(date_label, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 4; ++i) {
      if (clock_shadow[i]) lv_obj_add_flag(clock_shadow[i], LV_OBJ_FLAG_HIDDEN);
      if (date_shadow[i]) lv_obj_add_flag(date_shadow[i], LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }
  if (wanted.version == 3 && !clock_panel) {
    clock_panel = lv_obj_create(overlay); lv_obj_remove_style_all(clock_panel);
    lv_obj_set_pos(clock_panel, 24, 448); lv_obj_set_height(clock_panel, 128);
    lv_obj_set_style_bg_color(clock_panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(clock_panel, 69, 0);
    lv_obj_set_style_border_color(clock_panel, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_opa(clock_panel, 82, 0);
    lv_obj_set_style_border_width(clock_panel, 1, 0);
    lv_obj_set_style_radius(clock_panel, 16, 0);
    lv_obj_clear_flag(clock_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(clock_panel, LV_OBJ_FLAG_CLICKABLE);
  }
  if (clock_panel) {
    int widths[6]; measure_clock_widths(widths);
    lv_obj_set_width(clock_panel, widths[language_index(wanted.locale) * 2 + (!strcmp(wanted.clock_format, "12h") ? 1 : 0)]);
    lv_obj_clear_flag(clock_panel, LV_OBJ_FLAG_HIDDEN);
  }
  if (!clock_label) {
    static const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
    for (int i = 0; i < 4; ++i) {
      clock_shadow[i] = lv_label_create(overlay); date_shadow[i] = lv_label_create(overlay);
      lv_obj_set_style_text_font(clock_shadow[i], &font_photo_clock_80, 0);
      lv_obj_set_style_text_font(date_shadow[i], &font_montserrat_16_cyr, 0);
      lv_obj_set_pos(clock_shadow[i], 38 + dx[i], 475 + dy[i]);
      lv_obj_set_pos(date_shadow[i], 40 + dx[i], 558 + dy[i]);
    }
    clock_label = lv_label_create(overlay); date_label = lv_label_create(overlay);
    lv_obj_set_style_text_color(clock_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_color(date_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(clock_label, &font_photo_clock_80, 0);
    lv_obj_set_style_text_font(date_label, &font_montserrat_16_cyr, 0);
    lv_obj_set_pos(clock_label, 38, 475); lv_obj_set_pos(date_label, 40, 558);
    last_minute = -1;
  }
  int clock_y = wanted.version == 3 ? 463 : 475, date_y = wanted.version == 3 ? 546 : 558;
  static const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
  lv_obj_set_pos(clock_label, 38, clock_y); lv_obj_set_pos(date_label, 40, date_y);
  for (int i = 0; i < 4; ++i) { lv_obj_set_pos(clock_shadow[i], 38 + dx[i], clock_y + dy[i]); lv_obj_set_pos(date_shadow[i], 40 + dx[i], date_y + dy[i]); }
  bool dark = wanted.version == 2 && !strcmp(wanted.tone, "dark");
  lv_obj_set_style_text_color(clock_label, lv_color_hex(dark ? 0x111111 : 0xffffff), 0);
  lv_obj_set_style_text_color(date_label, lv_color_hex(dark ? 0x111111 : 0xffffff), 0);
  for (int i = 0; i < 4; ++i) {
    lv_obj_set_style_text_color(clock_shadow[i], lv_color_hex(dark ? 0xffffff : 0x000000), 0);
    lv_obj_set_style_text_color(date_shadow[i], lv_color_hex(dark ? 0xffffff : 0x000000), 0);
    lv_obj_clear_flag(clock_shadow[i], LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(date_shadow[i], LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_clear_flag(clock_label, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(date_label, LV_OBJ_FLAG_HIDDEN);
  int minute = t.tm_yday * 1440 + t.tm_hour * 60 + t.tm_min;
  if (last_minute == minute) return;
  last_minute = minute; char buf[128];
  format_time(buf, sizeof(buf), t.tm_hour, t.tm_min, !strcmp(wanted.clock_format, "12h"));
  lv_label_set_text(clock_label, buf); for (auto *label : clock_shadow) lv_label_set_text(label, buf);
  format_date(buf, sizeof(buf), language_index(wanted.locale), t.tm_mday, t.tm_wday, t.tm_mon);
  lv_label_set_text(date_label, buf); for (auto *label : date_shadow) lv_label_set_text(label, buf);
}

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
  discard_pending();
  clear_controls();
  clear_context();
  context_poll = 0;
  slideshow_paused = false;
  info_hidden = false;
  load_error = false;
  retry_requested = false;
  stale_refresh_pending = false;
  manual_target = -1;
  manual_target_pending = false;
  manual_target_id[0] = 0;
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
    return false;
  }
  return true;
}
static void publish(int index, const char *id) {
  if (strcmp(shown_id, id) && current_manifest.scene_context_version != 3) { clear_context(); context_poll = 0; card_retry = 0; }
  if (!overlay) {
    ui_dashboard_cancel_brightness_test();
    ui_dashboard_hide_sensitive();
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
  load_error = false;
  retry_requested = false;
  if (manual_target_pending && !strcmp(manual_target_id, id)) {
    manual_target_pending = false;
    manual_target = -1;
    manual_target_id[0] = 0;
  }
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
      else if (local.kind == CONTEXT)
        result = photo_fetch_context(bridge_url, bridge_key, &local.context, cancelled, &local.generation, &local.diagnostic, local.version, local.bound_frame);
      else if (local.kind == CARD)
        result = photo_fetch_overlay(bridge_url, bridge_key, &local.card, local.staging, cancelled, &local.generation, &local.diagnostic, local.version);
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
static bool submit(job_kind kind, int index, const photo_frame_entry *entry, const photo_overlay_block *card = nullptr, uint8_t *staging = nullptr, const char *bound = nullptr) {
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
  job.request_start_us = esp_timer_get_time();
  job.buffer = index;
  job.version = current_manifest.scene_context_version == 3 ? 3 : current_manifest.overlay_context_version == 2 ? 2 : 1;
  if (kind == CONTEXT || kind == CARD) strncpy(job.bound_frame, bound ? bound : shown_id, 64);
  if (entry)
    job.frame = *entry;
  if (card) job.card = *card;
  job.staging = staging;
  busy.store(true);
  xSemaphoreGive(mutex);
  xTaskNotifyGive(target);

  return true;
}
static void retry() {
  static const int delays[] = {30, 60, 120, 300};
  retry_after = now() + delays[retry_step < 4 ? retry_step : 3];
  if (retry_step < 3)
    retry_step++;
}
static int frame_index(const char *id) {
  for (int i = 0; i < current_manifest.count; ++i)
    if (!strcmp(id, current_manifest.frames[i].id)) return i;
  return -1;
}
static void request_navigation(int direction) {
  if (!manifest_ready || current_manifest.count <= 0) {
    load_error = true;
    last_poll = 0;
    retry_after = 0;
    update_controls();
    return;
  }
  int base = manual_target_pending ? frame_index(manual_target_id) : frame_index(shown_id);
  if (base < 0) base = direction > 0 ? -1 : 0;
  manual_target = (base + direction + current_manifest.count) % current_manifest.count;
  strncpy(manual_target_id, current_manifest.frames[manual_target].id, 64);
  manual_target_id[64] = 0;
  if (overlay && !strcmp(shown_id, manual_target_id)) {
    // Returning to the visible frame supersedes any in-flight request just as
    // selecting a different target does. Its late completion must stay stale.
    epoch.fetch_add(1);
    discard_pending();
    manual_target_pending = false;
    manual_target = -1;
    manual_target_id[0] = 0;
    load_error = false;
    retry_after = 0;
    controls_until = now() + 5;
    update_controls();
    return;
  }
  manual_target_pending = true;
  epoch.fetch_add(1);
  discard_pending();
  load_error = false;
  retry_after = 0;
  controls_until = now() + 5;
  update_controls();
}
static void execute_control(int action) {
  switch (action) {
  case 0: request_navigation(-1); break;
  case 1: request_navigation(1); break;
  case 2:
    if (slideshow_paused) { slideshow_paused = false; last_slide = now(); }
    else slideshow_paused = true;
    update_controls();
    break;
  case 3:
    set_info_hidden(!info_hidden);
    update_controls();
    break;
  case 4:
    load_error = false;
    stale_refresh_pending = false;
    retry_after = 0;
    retry_step = 0;
    last_poll = 0;
    retry_requested = true;
    update_controls();
    break;
  case 5:
    manual_requested = false;
    slideshow_paused = false;
    info_hidden = false;
    load_error = false;
    manual_target_pending = false;
    manual_target = -1;
    manual_target_id[0] = 0;
    detach();
    last_touch = now();
    ui_dashboard_photo_exit();
    break;
  }
}
static int control_at(int x, int y) {
  if (!controls_visible) return -1;
  const int count = load_error ? 6 : 5;
  for (int i = 0; i < count; ++i) {
    lv_area_t coords;
    lv_obj_get_coords(control_buttons[i], &coords);
    if (x >= coords.x1 && x <= coords.x2 && y >= coords.y1 && y <= coords.y2)
      return (i == count - 1) ? 5 : i;
  }
  return -1;
}
static void maybe_open_photo_controls() {
  if (!physical_down || !touch_started_in_photo || !overlay || touch_start_action >= 0 ||
      touch_hold_opened || touch_hold_cancelled) return;
  if (esp_timer_get_time() - touch_started_us < TOUCH_HOLD_US) return;
  touch_hold_opened = true;
  show_controls();
}
static bool process_photo_touch(bool pressed, int x, int y) {
  int64_t t = now();
  if (pressed) {
    // GT911 supplies coordinates only while contact is present. Cache every
    // pressed sample, including movement; release coordinates are undefined.
    touch_last_x = x;
    touch_last_y = y;
    if (physical_down) {
      if (abs(x - touch_start_x) > TOUCH_TAP_SLOP || abs(y - touch_start_y) > TOUCH_TAP_SLOP)
        touch_hold_cancelled = true;
      maybe_open_photo_controls();
      return touch_started_in_photo;
    }
    physical_down = true;
    last_touch = t;
    touch_start_x = x;
    touch_start_y = y;
    touch_started_in_photo = overlay != nullptr;
    touch_hold_opened = touch_hold_cancelled = false;
    touch_started_us = esp_timer_get_time();
    touch_start_action = controls_visible ? control_at(x, y) : -1;
    gesture = touch_started_in_photo;
    if (!touch_started_in_photo) {
      epoch.fetch_add(1);
      manual_requested = false;
      return false;
    }
    return true;
  }
  maybe_open_photo_controls();
  physical_down = false;
  if (!touch_started_in_photo) {
    touch_started_in_photo = false;
    gesture = false;
    return false;
  }
  int dx = touch_last_x - touch_start_x, dy = touch_last_y - touch_start_y;
  if (touch_hold_opened) {
    // The release that finishes the hold must neither exit nor press a button
    // newly revealed underneath the finger.
    controls_until = t + 5;
  } else if (abs(dx) >= 60 && abs(dy) < abs(dx) / 2) {
    request_navigation(dx < 0 ? 1 : -1);
  } else if (!touch_hold_cancelled && abs(dx) <= TOUCH_TAP_SLOP && abs(dy) <= TOUCH_TAP_SLOP) {
    if (touch_start_action >= 0) {
      if (control_at(touch_last_x, touch_last_y) == touch_start_action) {
        controls_until = t + 5;
        execute_control(touch_start_action);
      }
    } else {
      execute_control(5);
    }
  }
  touch_start_action = -1;
  touch_hold_opened = touch_hold_cancelled = false;
  touch_started_in_photo = false;
  gesture = false;
  return true;
}
static void schedule_info() {
  if (overlay && !network_update_pending && ((current_manifest.scene_context_version == 3 && protocol_negotiated) || shown_in_manifest()) && !busy.load() && (context_poll == 0 || esp_timer_get_time() / 1000000 - context_poll >= 60)) {
    if (submit(CONTEXT, 0, nullptr)) context_poll = esp_timer_get_time() / 1000000;
  }
  if (overlay && !network_update_pending && ((current_manifest.scene_context_version == 3 && protocol_negotiated) || shown_in_manifest()) && context_known && !busy.load() && esp_timer_get_time() / 1000000 >= card_retry) {
    for (int i = 0; i < wanted.count; ++i) {
      photo_overlay_block &b = wanted.blocks[i]; int n = slot(b.kind);
      if (b.expires_at <= server_now() || (cards[n].pixels && !strcmp(cards[n].meta.id, b.id) && !strcmp(cards[n].meta.sha256, b.sha256) && cards[n].meta.width == b.width && cards[n].meta.height == b.height)) continue;
      size_t bytes = (size_t)b.width * b.height * (current_manifest.scene_context_version == 3 ? 4 : 2);
      size_t live = 0;
      for (int j = 0; j < 3; ++j) if (cards[j].pixels)
        live += (size_t)cards[j].meta.width * cards[j].meta.height * (current_manifest.scene_context_version == 3 ? 4 : 2);
      uint8_t *staging = live + bytes <= 768u * 1024u ? (uint8_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr;
      if (!staging) { card_retry = esp_timer_get_time() / 1000000 + 60; break; }
#ifdef SIMULATOR
      if (live + bytes > sim_pixel_peak) sim_pixel_peak = live + bytes;
#endif
      if (!submit(CARD, 0, nullptr, &b, staging)) free(staging);
      break;
    }
  }
}
static void tick(lv_timer_t *) {
  if (!mutex || paused.load())
    return;
  xSemaphoreTake(mutex, portMAX_DELAY);
  if (network_update_pending) {
    clear_context(); context_poll = 0; manifest_ready = false; protocol_negotiated = false;
    if (!busy.load()) {
      strncpy(bridge_url, pending_bridge_url, sizeof(bridge_url) - 1);
      strncpy(bridge_key, pending_bridge_key, sizeof(bridge_key) - 1);
      bridge_url[sizeof(bridge_url) - 1] = 0;
      bridge_key[sizeof(bridge_key) - 1] = 0;
      network_update_pending = false;
      last_ready = 0; last_poll = 0; retry_after = 0;
    }
  }
  xSemaphoreGive(mutex);
  if (!web_photos_enabled) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (job.complete) { if (job.staging) free(job.staging); job = {}; }
    xSemaphoreGive(mutex);
    manual_requested = false;
    if (busy.load()) return;
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
                  (manual_target_pending || retry_requested || manual_requested || t - last_touch >= idle_seconds);
  if (pending.ready && (pending.generation != epoch.load() || !frame_in_manifest(pending.id))) discard_pending();
  if (completed && result.generation == epoch.load()) {
    if (result.kind == CONTEXT && overlay && (result.version == 3 || (shown_in_manifest() && !strcmp(result.bound_frame, shown_id)))) {
      if (result.result == PHOTO_NOT_FOUND && result.version != 3) clear_context();
      else if (result.result == PHOTO_OK && overlay) {
        // A delayed response cannot move the estimated server clock backwards.
        int64_t anchor = result.context.server_time + (esp_timer_get_time() - result.request_start_us) / 1000000;
        if (context_known && anchor < server_now()) anchor = server_now();
        server_anchor = anchor; monotonic_anchor = esp_timer_get_time();
        bool clock_changed = !context_known || wanted.clock_enabled != result.context.clock_enabled ||
          strcmp(wanted.locale, result.context.locale) || strcmp(wanted.clock_format, result.context.clock_format) ||
          strcmp(wanted.timezone, result.context.timezone) || strcmp(wanted.tone, result.context.tone);
        wanted = result.context; context_known = true;
        if (clock_changed) last_minute = -1;
        bool present[3] = {};
        for (int i = 0; i < wanted.count; ++i) {
          int n = slot(wanted.blocks[i].kind); present[n] = true;
          if (wanted.blocks[i].expires_at <= server_now()) clear_card(n);
          else if (cards[n].pixels && !strcmp(cards[n].meta.id, wanted.blocks[i].id) && !strcmp(cards[n].meta.sha256, wanted.blocks[i].sha256) && cards[n].meta.width == wanted.blocks[i].width && cards[n].meta.height == wanted.blocks[i].height) {
            cards[n].meta = wanted.blocks[i]; lv_obj_set_pos(cards[n].object, cards[n].meta.x, cards[n].meta.y);
          }
        }
        for (int i = 0; i < 3; ++i) if (!present[i]) clear_card(i);
      }
    } else if (result.kind == CARD && overlay && (result.version == 3 || (shown_in_manifest() && !strcmp(result.bound_frame, shown_id)))) {
      if (result.result != PHOTO_OK) card_retry = esp_timer_get_time() / 1000000 + 60;
      int n = slot(result.card.kind);
      if (result.result == PHOTO_OK && overlay && context_known && result.card.expires_at > server_now()) {
        bool current = false;
        for (int i = 0; i < wanted.count; ++i) if (!strcmp(wanted.blocks[i].id, result.card.id) && !strcmp(wanted.blocks[i].sha256, result.card.sha256) && wanted.blocks[i].width == result.card.width && wanted.blocks[i].height == result.card.height && wanted.blocks[i].expires_at > server_now()) current = true;
        if (current) {
          clear_card(n); cards[n].meta = result.card; cards[n].pixels = result.staging; result.staging = nullptr;
          lv_image_dsc_t &d = cards[n].desc; d.header.magic = LV_IMAGE_HEADER_MAGIC; d.header.cf = result.version == 3 ? LV_COLOR_FORMAT_ARGB8888 : LV_COLOR_FORMAT_RGB565;
          int pixel_bytes = result.version == 3 ? 4 : 2;
          d.header.w = result.card.width; d.header.h = result.card.height; d.header.stride = result.card.width * pixel_bytes;
          d.data_size = (size_t)result.card.width * result.card.height * pixel_bytes; d.data = cards[n].pixels;
          cards[n].object = lv_obj_create(overlay); lv_obj_remove_style_all(cards[n].object);
          lv_obj_set_size(cards[n].object, result.card.width, result.card.height);
          if (result.version == 3) {
            lv_obj_set_style_bg_color(cards[n].object, lv_color_hex(0x000000), 0);
            lv_obj_set_style_bg_opa(cards[n].object, 69, 0);
            lv_obj_set_style_border_color(cards[n].object, lv_color_hex(0xffffff), 0);
            lv_obj_set_style_border_opa(cards[n].object, 82, 0);
            lv_obj_set_style_border_width(cards[n].object, 1, 0);
            lv_obj_set_style_radius(cards[n].object, 16, 0);
          } else if (result.version == 1) {
            lv_obj_set_style_radius(cards[n].object, 3, 0);
            lv_obj_set_style_clip_corner(cards[n].object, true, 0);
          }
          lv_obj_clear_flag(cards[n].object, LV_OBJ_FLAG_SCROLLABLE);
          lv_obj_clear_flag(cards[n].object, LV_OBJ_FLAG_CLICKABLE);
          lv_obj_t *card_image = lv_image_create(cards[n].object);
          lv_image_set_src(card_image, &d); lv_obj_set_pos(card_image, 0, 0);
          lv_obj_set_pos(cards[n].object, result.card.x, result.card.y);
          for (int i = 0; i < 4; ++i) {
            if (clock_shadow[i]) lv_obj_move_foreground(clock_shadow[i]);
            if (date_shadow[i]) lv_obj_move_foreground(date_shadow[i]);
          }
          if (clock_label) lv_obj_move_foreground(clock_label);
          if (date_label) lv_obj_move_foreground(date_label);
        }
      }
    } else
    if (result.kind == MANIFEST && result.result == PHOTO_OK) {
      photo_manifest &m = result.manifest;
      if (!web_timing_enabled) {
        idle_seconds = m.idle;
        interval_seconds = m.interval;
      }
      stale_seconds = m.stale;
      if (!strcmp(m.state, "ready") || m.scene_context_version == 3 || current_manifest.scene_context_version == 3) {
        if (current_manifest.overlay_context_version != m.overlay_context_version || current_manifest.scene_context_version != m.scene_context_version) { discard_pending(); clear_context(); context_poll = 0; }
        current_manifest = m;
      }
      protocol_negotiated = true;
      eligible = !physical_down && !ui_dashboard_reader_open() &&
                 (manual_target_pending || retry_requested || manual_requested || t - last_touch >= idle_seconds);
      if (!strcmp(m.state, "disabled") || !strcmp(m.state, "empty")) {
        detach();
        free_buffers();
        last_ready = 0;
        retry_step = 0;
        manifest_ready = false;
        manual_requested = false;
      } else if (!strcmp(m.state, "ready")) {
        last_ready = t;
        retry_step = 0;
        retry_after = 0;
        manifest_ready = true;
        if (manual_target_pending) {
          int found = frame_index(manual_target_id);
          if (found < 0) {
            manual_target_pending = false;
            manual_target = -1;
            manual_target_id[0] = 0;
          } else manual_target = found;
        }
        load_error = false;
        if (stale_refresh_pending) {
          stale_refresh_pending = false;
          retry_requested = false;
        }
        if (overlay && !shown_in_manifest() && m.scene_context_version != 3) { clear_context(); context_poll = 0; }
      } else {
        manifest_ready = false;
        retry();
        load_error = overlay != nullptr;
      }
    } else if (result.kind == FRAME && result.result == PHOTO_OK && eligible) {
      if (result.version == 3) {
        pending.ready = true; pending.generation = result.generation; pending.buffer = result.buffer;
        strncpy(pending.id, result.frame.id, 64);
      } else publish(result.buffer, result.frame.id);
      retry_step = 0;
      retry_after = 0;
    } else if (result.result == PHOTO_NOT_FOUND && result.kind == FRAME) {
      last_poll = 0;
      retry_after = 0;
      load_error = overlay != nullptr;
    } else if (result.result != PHOTO_CANCELLED) {
      retry();
      if (result.kind == FRAME || result.kind == MANIFEST) load_error = overlay != nullptr;
    }
  }
  if (completed && result.staging) free(result.staging);
  if (overlay && context_known) {
    for (int i = 0; i < 3; ++i) if (cards[i].pixels && cards[i].meta.expires_at <= server_now()) clear_card(i);
    render_clock();
  }
  if (info_hidden) set_info_hidden(true);
  maybe_open_photo_controls();
  if (controls_visible && t >= controls_until && !physical_down) clear_controls();
  if (controls_visible) update_controls();
  controls_raise();
  if (last_ready && t - last_ready >= stale_seconds) {
    const bool was_visible = overlay != nullptr;
    epoch.fetch_add(1);
    discard_pending();
    manifest_ready = false;
    protocol_negotiated = false;
    last_ready = 0;
    last_poll = 0;
    retry_after = 0;
    retry_step = 0;
    clear_context();
    manual_target_pending = false;
    manual_target = -1;
    manual_target_id[0] = 0;
    if (was_visible) {
      load_error = true;
      retry_requested = true;
      stale_refresh_pending = true;
      eligible = true;
      update_controls();
    } else {
      detach();
      manual_requested = false;
    }
  }
  if (current_manifest.scene_context_version != 3) schedule_info();
  if (eligible && !busy.load() && t >= retry_after &&
      (last_poll == 0 || t - last_poll >= 60)) {
    if (submit(MANIFEST, 0, nullptr))
      last_poll = t;
  }
  if (current_manifest.scene_context_version != 3 && eligible && manifest_ready && !busy.load() && t >= retry_after &&
      current_manifest.count > 0 &&
      (manual_target_pending || retry_requested || !overlay || (!slideshow_paused && t - last_slide >= interval_seconds))) {
    int next_frame = 0;
    if (manual_target_pending) next_frame = manual_target;
    else if (overlay) {
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
    else if (manual_target_pending && !strcmp(shown_id, entry->id)) {
      manual_target_pending = false;
      manual_target = -1;
      manual_target_id[0] = 0;
    }
  }
  if (current_manifest.scene_context_version == 3 && eligible && manifest_ready && !network_update_pending) {
    bool manual_pending_ready = manual_target_pending && pending.ready && !strcmp(pending.id, manual_target_id);
    if (pending.ready && pending.generation == epoch.load() && frame_in_manifest(pending.id) && (!overlay || manual_pending_ready || (!slideshow_paused && t - last_slide >= interval_seconds))) {
      publish(pending.buffer, pending.id); discard_pending();
    }
    int lead = interval_seconds / 2 < 15 ? interval_seconds / 2 : 15;
    if (!pending.ready && !busy.load() && t >= retry_after && current_manifest.count > 0 &&
        (manual_target_pending || retry_requested || !overlay || (!slideshow_paused && ((!shown_in_manifest() || current_manifest.count > 1) && t - last_slide >= interval_seconds - lead)))) {
      int next = 0;
      if (manual_target_pending) next = manual_target;
      else for (int i = 0; i < current_manifest.count; ++i) if (!strcmp(shown_id, current_manifest.frames[i].id)) { next = (i + 1) % current_manifest.count; break; }
      if (allocate_buffers()) submit(FRAME, displayed == 0 ? 1 : 0, &current_manifest.frames[next]);
    }
  }
  if (current_manifest.scene_context_version == 3) schedule_info();

}
} // namespace
void photo_frame_ui_init() {
  mutex = xSemaphoreCreateMutex();
  last_touch = now();
  lv_timer_create(tick, 1000, nullptr);
}
void photo_frame_confirm_timezone(const char *timezone, const char *posix_timezone) {
  if (!timezone || !posix_timezone || strlen(timezone) >= sizeof(confirmed_timezone) || strlen(posix_timezone) >= sizeof(confirmed_posix_timezone) || !lvgl_port_lock(-1)) return;
  strcpy(confirmed_timezone, timezone); strcpy(confirmed_posix_timezone, posix_timezone); last_minute = -1;
  lvgl_port_unlock();
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
    return false;
  }
  if (paused.load()) {
    return false;
  }
  if (!web_photos_enabled) {
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
  tick(nullptr);
  return true;
}
bool photo_frame_touch(bool pressed) {
  return process_photo_touch(pressed, 0, 0);
}
void photo_frame_filter_touch(bool pressed, uint16_t x, uint16_t y,
                              lv_indev_data_t *data) {
  static bool backlight_passthrough = false;
  if (pressed && !physical_down && ui_dashboard_backlight_preview_contains(x, y)) backlight_passthrough = true;
  if (backlight_passthrough) {
    data->point.x = x; data->point.y = y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    if (!pressed) backlight_passthrough = false;
    return;
  }
  if (process_photo_touch(pressed, x, y)) {
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
    if (job.complete && job.staging) free(job.staging);
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
bool photo_frame_sim_visible() { return overlay != nullptr; }
#endif

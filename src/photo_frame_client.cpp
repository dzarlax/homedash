#include "photo_frame_client.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "web_identity.h"
#include "mbedtls/sha256.h"
#include <stdio.h>
#include <string.h>

namespace {
struct response_headers {
  bool encoded = false;
  bool ranged = false;
  bool octet_stream = false;
};
esp_err_t on_event(esp_http_client_event_t *e) {
  response_headers *h = static_cast<response_headers *>(e->user_data);
  if (e->event_id == HTTP_EVENT_ON_HEADER && e->header_key && e->header_value) {
    if (!strcasecmp(e->header_key, "Content-Encoding") &&
        strcasecmp(e->header_value, "identity"))
      h->encoded = true;
    if (!strcasecmp(e->header_key, "Content-Type") && !strcasecmp(e->header_value, "application/octet-stream"))
      h->octet_stream = true;
    if (!strcasecmp(e->header_key, "Content-Range"))
      h->ranged = true;
  }
  return ESP_OK;
}
struct request {
  esp_http_client_handle_t client = nullptr;
  response_headers headers;
  int64_t deadline = 0;
  photo_diagnostic diagnostic;
  photo_diagnostic *output;
  explicit request(photo_diagnostic *out) : output(out) {}
  ~request() {
    if (output) *output = diagnostic;
    if (client) {
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
    }
  }
  photo_result open(const char *url, const char *key, photo_cancel_fn cancel,
                    void *arg, int64_t budget_us = 15000000) {
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 2000;
    config.disable_auto_redirect = true;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.event_handler = on_event;
    config.user_data = &headers;
    diagnostic.stage = "init";
    client = esp_http_client_init(&config);
    if (!client)
      return PHOTO_FAILED;
    esp_http_client_set_header(client, "X-API-Key", key);
    if (web_identity_set_http_header(client) != ESP_OK) {
      diagnostic.stage = "device_id";
      return PHOTO_FAILED;
    }
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    deadline = esp_timer_get_time() + budget_us;
    photo_result result = check(cancel, arg);
    if (result != PHOTO_OK)
      return result;
    diagnostic.stage = "connect";
    diagnostic.transport_error = esp_http_client_open(client, 0);
    if (diagnostic.transport_error != ESP_OK)
      return check(cancel, arg) == PHOTO_CANCELLED ? PHOTO_CANCELLED
                                                   : PHOTO_FAILED;
    result = check(cancel, arg);
    if (result != PHOTO_OK)
      return result;
    diagnostic.stage = "headers";
    int64_t header_result = esp_http_client_fetch_headers(client);
    if (header_result < 0)
      diagnostic.transport_error = (int)header_result;
    diagnostic.http_status = esp_http_client_get_status_code(client);
    if (header_result < 0)
      return check(cancel, arg) == PHOTO_CANCELLED ? PHOTO_CANCELLED
                                                   : PHOTO_FAILED;
    result = check(cancel, arg);
    if (result != PHOTO_OK)
      return result;
    diagnostic.stage = "encoding";
    return headers.encoded || headers.ranged ? PHOTO_FAILED : PHOTO_OK;
  }
  photo_result check(photo_cancel_fn cancel, void *arg) {
    if (cancel && cancel(arg))
      return PHOTO_CANCELLED;
    if (esp_timer_get_time() >= deadline) {
      diagnostic.stage = "deadline";
      return PHOTO_FAILED;
    }
    return PHOTO_OK;
  }
};
bool hex64(const char *s) {
  if (!s || strlen(s) != 64)
    return false;
  for (int i = 0; i < 64; i++)
    if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
      return false;
  return true;
}
bool unique_object_keys(const cJSON *node) {
  if (cJSON_IsObject(node)) {
    for (const cJSON *a = node->child; a; a = a->next) {
      if (!a->string) return false;
      for (const cJSON *b = a->next; b; b = b->next)
        if (b->string && !strcmp(a->string, b->string)) return false;
    }
  }
  for (const cJSON *child = node ? node->child : nullptr; child; child = child->next)
    if (!unique_object_keys(child)) return false;
  return true;
}
bool number(cJSON *root, const char *name, int min, int max, int *out) {
  cJSON *v = cJSON_GetObjectItemCaseSensitive(root, name);
  if (!cJSON_IsNumber(v) || v->valuedouble != v->valueint ||
      v->valueint < min || v->valueint > max)
    return false;
  *out = v->valueint;
  return true;
}
bool text(cJSON *root, const char *name, char *out, size_t cap) {
  cJSON *v = cJSON_GetObjectItemCaseSensitive(root, name);
  if (!cJSON_IsString(v) || !v->valuestring || strlen(v->valuestring) >= cap)
    return false;
  strcpy(out, v->valuestring);
  return true;
}
} // namespace
photo_result photo_fetch_manifest(const char *base, const char *key,
                                  photo_manifest *out, photo_cancel_fn cancel,
                                  void *arg, photo_diagnostic *diagnostic) {
  if (diagnostic) *diagnostic = {};
  char url[256];
  if (snprintf(url, sizeof(url), "%s/api/photo-frame/v1/manifest", base) >=
      (int)sizeof(url))
    return PHOTO_FAILED;
  request r(diagnostic);
  photo_result opened = r.open(url, key, cancel, arg);
  if (opened != PHOTO_OK)
    return opened;
  r.diagnostic.stage = "HTTP";
  int status = esp_http_client_get_status_code(r.client);
  if (status == 404)
    return PHOTO_NOT_FOUND;
  if (status != 200)
    return PHOTO_FAILED;
  r.diagnostic.stage = "chunked";
  if (esp_http_client_is_chunked_response(r.client))
    return PHOTO_FAILED;
  r.diagnostic.stage = "length";
  int64_t length = esp_http_client_get_content_length(r.client);
  if (length < 1 || length > 8192)
    return PHOTO_FAILED;
  static char body[8193];
  r.diagnostic.stage = "read";
  size_t used = 0;
  while (used < (size_t)length) {
    photo_result check = r.check(cancel, arg);
    if (check != PHOTO_OK)
      return check;
    int remaining_ms = (int)((r.deadline - esp_timer_get_time()) / 1000);
    if (remaining_ms < 1)
      remaining_ms = 1;
    esp_http_client_set_timeout_ms(r.client,
                                   remaining_ms < 2000 ? remaining_ms : 2000);
    int n = esp_http_client_read(r.client, body + used, (int)(length - used));
    if (n == -ESP_ERR_HTTP_EAGAIN)
      continue;
    if (n <= 0) {
      r.diagnostic.stage = n == 0 ? "EOF" : "read";
      r.diagnostic.transport_error = n;
      return PHOTO_FAILED;
    }
    used += n;
    r.diagnostic.received = used;
  }
  body[used] = '\0';
  if (r.check(cancel, arg) != PHOTO_OK)
    return r.check(cancel, arg);
  r.diagnostic.stage = "complete";
  if (!esp_http_client_is_complete_data_received(r.client))
    return PHOTO_FAILED;
  r.diagnostic.stage = "JSON";
  cJSON *root = cJSON_ParseWithLength(body, used);
  if (!root)
    return PHOTO_FAILED;
  r.diagnostic.stage = "schema";
  photo_manifest m = {};
  int v, w, h, bytes;
  char format[16];
  bool ok = cJSON_IsObject(root) && unique_object_keys(root) && number(root, "version", 1, 1, &v) &&
            number(root, "width", 1024, 1024, &w) &&
            number(root, "height", 600, 600, &h) &&
            number(root, "frame_bytes", PHOTO_FRAME_BYTES, PHOTO_FRAME_BYTES,
                   &bytes) &&
            number(root, "idle_after_s", 30, 3600, &m.idle) &&
            number(root, "slide_interval_s", 15, 3600, &m.interval) &&
            number(root, "max_stale_s", 60, 3600, &m.stale) &&
            text(root, "pixel_format", format, sizeof(format)) &&
            !strcmp(format, "rgb565le") &&
            text(root, "state", m.state, sizeof(m.state)) &&
            text(root, "generation", m.generation, sizeof(m.generation));
  m.overlay_context_version = 1;
  cJSON *overlay_version = cJSON_GetObjectItemCaseSensitive(root, "overlay_context_version");
  if (overlay_version && !number(root, "overlay_context_version", 1, 2, &m.overlay_context_version)) ok = false;
  cJSON *frames = cJSON_GetObjectItemCaseSensitive(root, "frames");
  if (!cJSON_IsArray(frames))
    ok = false;
  else {
    m.count = cJSON_GetArraySize(frames);
    if (m.count > 8)
      ok = false;
    for (int i = 0; ok && i < m.count; i++) {
      cJSON *f = cJSON_GetArrayItem(frames, i);
      ok = text(f, "id", m.frames[i].id, sizeof(m.frames[i].id)) &&
           hex64(m.frames[i].id) &&
           text(f, "sha256", m.frames[i].sha256, sizeof(m.frames[i].sha256)) &&
           hex64(m.frames[i].sha256);
    }
  }
  bool ready = !strcmp(m.state, "ready");
  if ((!ready && strcmp(m.state, "disabled") && strcmp(m.state, "empty") &&
       strcmp(m.state, "warming") && strcmp(m.state, "unavailable")) ||
      (ready ? m.count == 0 : m.count != 0))
    ok = false;
  cJSON_Delete(root);
  if (!ok)
    return PHOTO_FAILED;
  r.diagnostic.stage = "ok";
  *out = m;
  return PHOTO_OK;
}
static photo_result fetch_image(const char *base, const char *key,
                                const photo_frame_entry *entry, uint8_t *buffer, size_t bytes, const char *route, int version,
                                photo_cancel_fn cancel, void *arg, photo_diagnostic *diagnostic) {
  if (diagnostic) *diagnostic = {};
  if (!hex64(entry->id) || !hex64(entry->sha256))
    return PHOTO_FAILED;
  char url[256];
  if (snprintf(url, sizeof(url), "%s/api/photo-frame/v%d/%s/%s", base, version, route,
               entry->id) >= (int)sizeof(url))
    return PHOTO_FAILED;
  request r(diagnostic);
  photo_result opened = r.open(url, key, cancel, arg, 60000000);
  if (opened != PHOTO_OK)
    return opened;
  r.diagnostic.stage = "HTTP";
  int status = esp_http_client_get_status_code(r.client);
  if (status == 404)
    return PHOTO_NOT_FOUND;
  if (status != 200 || (!strcmp(route, "overlays") && !r.headers.octet_stream) || esp_http_client_is_chunked_response(r.client) ||
      esp_http_client_get_content_length(r.client) != (int64_t)bytes)
    return PHOTO_FAILED;
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  mbedtls_sha256_starts(&hash, 0);
  r.diagnostic.stage = "read";
  size_t used = 0;
  photo_result result = PHOTO_FAILED;
  while (used < bytes) {
    result = r.check(cancel, arg);
    if (result != PHOTO_OK)
      goto done;
    result = PHOTO_FAILED;
    int remain = bytes - used;
    int remaining_ms = (int)((r.deadline - esp_timer_get_time()) / 1000);
    if (remaining_ms < 1)
      remaining_ms = 1;
    esp_http_client_set_timeout_ms(r.client,
                                   remaining_ms < 2000 ? remaining_ms : 2000);
    int n = esp_http_client_read(r.client, (char *)buffer + used,
                                 remain > 8192 ? 8192 : remain);
    if (n == -ESP_ERR_HTTP_EAGAIN)
      continue;
    if (n <= 0) {
      r.diagnostic.stage = n == 0 ? "EOF" : "read";
      r.diagnostic.transport_error = n;
      goto done;
    }
    mbedtls_sha256_update(&hash, buffer + used, n);
    used += n;
    r.diagnostic.received = used;
  }
  result = r.check(cancel, arg);
  if (result != PHOTO_OK)
    goto done;
  result = PHOTO_FAILED;
  r.diagnostic.stage = "complete";
  if (!esp_http_client_is_complete_data_received(r.client))
    goto done;
  {
    r.diagnostic.stage = "sha256";
    unsigned char digest[32];
    char hex[65];
    mbedtls_sha256_finish(&hash, digest);
    for (int i = 0; i < 32; i++)
      snprintf(hex + i * 2, 3, "%02x", digest[i]);
    if (!strcmp(hex, entry->sha256))
      result = r.check(cancel, arg);
  }
done:
  if (result == PHOTO_OK)
    r.diagnostic.stage = "ok";
  mbedtls_sha256_free(&hash);
  return result;
}

photo_result photo_fetch_pixels(const char *base, const char *key, const photo_frame_entry *entry, uint8_t *buffer, photo_cancel_fn cancel, void *arg, photo_diagnostic *diagnostic) {
  return fetch_image(base, key, entry, buffer, PHOTO_FRAME_BYTES, "frames", 1, cancel, arg, diagnostic);
}
photo_result photo_fetch_overlay(const char *base, const char *key, const photo_overlay_block *block, uint8_t *buffer, photo_cancel_fn cancel, void *arg, photo_diagnostic *diagnostic, int version) {
  if ((version != 1 && version != 2) || !block || !buffer || block->width < 1 || block->height < 1 || (size_t)block->width * block->height * 2 > (version == 2 ? 122880u : 107520u)) return PHOTO_FAILED;
  photo_frame_entry entry = {};
  memcpy(entry.id, block->id, sizeof(entry.id)); memcpy(entry.sha256, block->sha256, sizeof(entry.sha256));
  return fetch_image(base, key, &entry, buffer, (size_t)block->width * block->height * 2, "overlays", version, cancel, arg, diagnostic);
}

photo_result photo_fetch_context(const char *base, const char *key, photo_context *out,
                                 photo_cancel_fn cancel, void *arg, photo_diagnostic *diagnostic, int requested_version, const char *frame_id) {
  if (diagnostic) *diagnostic = {};
  char url[256];
  if (!out || (requested_version != 1 && requested_version != 2) || (requested_version == 2 && !hex64(frame_id))) return PHOTO_FAILED;
  int url_len = requested_version == 2 ? snprintf(url, sizeof(url), "%s/api/photo-frame/v2/context?frame_id=%s", base, frame_id) : snprintf(url, sizeof(url), "%s/api/photo-frame/v1/context", base);
  if (url_len < 0 || url_len >= (int)sizeof(url)) return PHOTO_FAILED;
  request r(diagnostic);
  photo_result opened = r.open(url, key, cancel, arg);
  if (opened != PHOTO_OK) return opened;
  int status = esp_http_client_get_status_code(r.client);
  if (status == 404) return PHOTO_NOT_FOUND;
  int64_t length = esp_http_client_get_content_length(r.client);
  if (status != 200 || esp_http_client_is_chunked_response(r.client) || length < 1 || length > 4096) return PHOTO_FAILED;
  char body[4097]; size_t used = 0;
  while (used < (size_t)length) {
    photo_result check = r.check(cancel, arg); if (check != PHOTO_OK) return check;
    int remaining_ms = (int)((r.deadline - esp_timer_get_time()) / 1000);
    esp_http_client_set_timeout_ms(r.client, remaining_ms < 2000 ? (remaining_ms > 0 ? remaining_ms : 1) : 2000);
    int n = esp_http_client_read(r.client, body + used, (int)(length - used));
    if (n == -ESP_ERR_HTTP_EAGAIN) continue;
    if (n <= 0) return PHOTO_FAILED;
    used += n;
  }
  if (r.check(cancel, arg) != PHOTO_OK) return r.check(cancel, arg);
  if (!esp_http_client_is_complete_data_received(r.client)) return PHOTO_FAILED;
  body[used] = 0;
  const char *end = nullptr;
  cJSON *root = cJSON_ParseWithLengthOpts(body, used + 1, &end, true);
  if (!root) return PHOTO_FAILED;
  photo_context c = {}; int version = 0;
  auto epoch_value = [](cJSON *obj, const char *key, int64_t *value) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(v) || v->valuedouble < 1 || v->valuedouble > 4102444800.0 || v->valuedouble != (double)(int64_t)v->valuedouble) return false;
    *value = (int64_t)v->valuedouble; return true;
  };
  cJSON *clock = cJSON_GetObjectItemCaseSensitive(root, "clock");
  cJSON *enabled = cJSON_GetObjectItemCaseSensitive(clock, "enabled");
  bool ok = cJSON_IsObject(root) && unique_object_keys(root) && number(root, "version", requested_version, requested_version, &version) && text(root, "revision", c.revision, sizeof(c.revision)) && hex64(c.revision) && epoch_value(root, "server_time", &c.server_time) && cJSON_IsObject(clock) && cJSON_IsBool(enabled) && text(clock, "locale", c.locale, sizeof(c.locale)) && (!strcmp(c.locale, "ru") || !strcmp(c.locale, "en") || !strcmp(c.locale, "sr-Latn")) && text(clock, "timezone", c.timezone, sizeof(c.timezone)) && c.timezone[0] && text(clock, "clock_format", c.clock_format, sizeof(c.clock_format)) && (!strcmp(c.clock_format, "12h") || !strcmp(c.clock_format, "24h"));
  c.clock_enabled = cJSON_IsTrue(enabled); c.version = version;
  if (requested_version == 2) {
    cJSON *backdrop = cJSON_GetObjectItemCaseSensitive(clock, "backdrop");
    ok = ok && text(root, "frame_id", c.frame_id, sizeof(c.frame_id)) && hex64(c.frame_id) && !strcmp(c.frame_id, frame_id) && text(clock, "tone", c.tone, sizeof(c.tone)) && (!strcmp(c.tone, "light") || !strcmp(c.tone, "dark")) && cJSON_IsBool(backdrop);
    c.backdrop = cJSON_IsTrue(backdrop);
  }
  cJSON *blocks = cJSON_GetObjectItemCaseSensitive(root, "blocks");
  if (!cJSON_IsArray(blocks)) ok = false;
  else {
    c.count = cJSON_GetArraySize(blocks); if (c.count > (requested_version == 2 ? 3 : 2)) ok = false;
    size_t total = 0, largest = 0; bool has_clock = false;
    for (int i = 0; ok && i < c.count; ++i) {
      photo_overlay_block &b = c.blocks[i]; cJSON *item = cJSON_GetArrayItem(blocks, i);
      ok = cJSON_IsObject(item) && text(item, "kind", b.kind, sizeof(b.kind)) && (!strcmp(b.kind, "weather") || !strcmp(b.kind, "event") || (requested_version == 2 && !strcmp(b.kind, "clock"))) && text(item, "render_id", b.id, sizeof(b.id)) && hex64(b.id) && text(item, "sha256", b.sha256, sizeof(b.sha256)) && hex64(b.sha256) && number(item, "x", 0, 1023, &b.x) && number(item, "y", 0, 599, &b.y) && number(item, "width", 1, !strcmp(b.kind, "weather") ? 236 : 480, &b.width) && number(item, "height", 1, !strcmp(b.kind, "weather") ? 200 : !strcmp(b.kind, "clock") ? 128 : 112, &b.height) && b.x + b.width <= 1024 && b.y + b.height <= 600 && epoch_value(item, "expires_at", &b.expires_at) && b.expires_at <= c.server_time + (!strcmp(b.kind, "event") ? 300 : 1800);
      for (int j = 0; j < i; ++j) if (!strcmp(c.blocks[j].kind, b.kind)) ok = false;
      size_t bytes = (size_t)b.width * b.height * 2; total += bytes; if (bytes > largest) largest = bytes;
      if (!strcmp(b.kind, "clock")) has_clock = true;
    }
    if (total + largest > 512u * 1024u || (requested_version == 2 && (has_clock != c.backdrop || (has_clock && !c.clock_enabled)))) ok = false;
  }
  cJSON_Delete(root); if (!ok) return PHOTO_FAILED;
  *out = c; return PHOTO_OK;
}

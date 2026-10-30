#pragma once
#include <stddef.h>
#include <stdint.h>

#define PHOTO_FRAME_BYTES (1024u * 600u * 2u)
struct photo_frame_entry {
  char id[65];
  char sha256[65];
};
struct photo_manifest {
  char state[16];
  char generation[65];
  int idle, interval, stale, count, overlay_context_version;
  photo_frame_entry frames[8];
};
enum photo_result { PHOTO_OK, PHOTO_NOT_FOUND, PHOTO_FAILED, PHOTO_CANCELLED };
struct photo_diagnostic {
  const char *stage = "request";
  int http_status = 0;
  int transport_error = 0;
  size_t received = 0;
};
typedef bool (*photo_cancel_fn)(void *);
photo_result photo_fetch_manifest(const char *url, const char *key,
                                  photo_manifest *out, photo_cancel_fn cancel,
                                  void *arg, photo_diagnostic *diagnostic = nullptr);
photo_result photo_fetch_pixels(const char *url, const char *key,
                                const photo_frame_entry *entry, uint8_t *buffer,
                                photo_cancel_fn cancel, void *arg, photo_diagnostic *diagnostic = nullptr);

struct photo_overlay_block {
  char kind[8], id[65], sha256[65];
  int x, y, width, height;
  int64_t expires_at;
};
struct photo_context {
  char revision[65], locale[8], timezone[40], clock_format[4], frame_id[65], tone[6];
  int64_t server_time;
  bool clock_enabled, backdrop;
  int version;
  int count;
  photo_overlay_block blocks[3];
};
photo_result photo_fetch_context(const char *, const char *, photo_context *, photo_cancel_fn, void *, photo_diagnostic * = nullptr, int version = 1, const char *frame_id = nullptr);
photo_result photo_fetch_overlay(const char *, const char *, const photo_overlay_block *, uint8_t *, photo_cancel_fn, void *, photo_diagnostic * = nullptr, int version = 1);

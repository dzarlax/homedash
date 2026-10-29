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
  int idle, interval, stale, count;
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

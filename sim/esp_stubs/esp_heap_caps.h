#pragma once
#include <cstdint>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t esp_get_free_heap_size(void);
void *heap_caps_malloc(size_t size, uint32_t caps);
#define MALLOC_CAP_SPIRAM 0
#ifdef __cplusplus
}
#endif

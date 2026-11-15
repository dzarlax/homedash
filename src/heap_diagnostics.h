#pragma once

#include "esp_heap_caps.h"
#include "esp_log.h"

// Numeric allocator state only. The stage must be a fixed, non-sensitive label.
static inline void log_heap_snapshot(const char *stage) {
    constexpr uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    constexpr uint32_t dma = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
    ESP_LOGI("heap_snapshot", "%s internal_free=%u largest=%u min=%u dma_free=%u largest=%u min=%u psram_free=%u",
             stage,
             (unsigned)heap_caps_get_free_size(internal),
             (unsigned)heap_caps_get_largest_free_block(internal),
             (unsigned)heap_caps_get_minimum_free_size(internal),
             (unsigned)heap_caps_get_free_size(dma),
             (unsigned)heap_caps_get_largest_free_block(dma),
             (unsigned)heap_caps_get_minimum_free_size(dma),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

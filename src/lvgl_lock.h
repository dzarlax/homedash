#pragma once

// UI locking without importing display drivers into network-only clients.
bool lvgl_port_lock(int timeout_ms);
void lvgl_port_unlock(void);

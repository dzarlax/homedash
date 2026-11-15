#pragma once
#include <stdint.h>
// UI init must run under the LVGL lock before start. Empty NVS disables push.
void voice_push_ui_init();
void voice_push_start();
void voice_push_touch();
bool voice_push_forget();
// Called only by the authorized, local recovery AP; takes effect after restart.
bool voice_push_provision(const char *base_url, const char *token);
enum VoicePriority : uint32_t { VOICE_PRIORITY_OTA=1, VOICE_PRIORITY_RECOVERY=2, VOICE_PRIORITY_NETWORK_TRIAL=4, VOICE_PRIORITY_NETWORK_UPDATE=8 };
// Never call while holding LVGL lock. A priority turn is suppressed, never queued.
void voice_push_priority(VoicePriority reason, bool enabled);

#ifdef SIMULATOR
void voice_push_sim_startup_mode(int mode);
void voice_push_sim_startup_stats(unsigned *out);
void voice_push_sim_connected();
void voice_push_sim_disconnected();
void voice_push_sim_advance(uint64_t ms);
void voice_push_sim_frame(const char *data, unsigned size, unsigned offset, unsigned total, unsigned opcode, bool fin);
void voice_push_sim_refresh();
void voice_push_sim_dismiss();
#endif

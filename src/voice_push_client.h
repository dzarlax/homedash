#pragma once
#include <stdint.h>
// UI init must run under the LVGL lock before start. Empty NVS disables push.
void voice_push_ui_init();
void voice_push_start();
// Call on the network task without the LVGL lock; serialize pairing store access.
#ifndef SIMULATOR
void voice_push_poll_start();
#endif
// Do not call while holding LVGL lock. Completion means client destroyed and bearer wiped.
void voice_push_request_stop();
bool voice_push_is_stopped();
void voice_push_touch();
bool voice_push_forget();
// Called only by the authorized, local recovery AP; takes effect after restart.
bool voice_push_provision(const char *base_url, const char *token);
enum VoicePriority : uint32_t { VOICE_PRIORITY_OTA=1, VOICE_PRIORITY_RECOVERY=2, VOICE_PRIORITY_NETWORK_TRIAL=4, VOICE_PRIORITY_NETWORK_UPDATE=8 };
// Never call while holding LVGL lock. A priority turn is suppressed, never queued.
void voice_push_priority(VoicePriority reason, bool enabled);

#ifdef SIMULATOR
// Inject the result of the simulator worker's destroy operation.
void voice_push_sim_finish_worker(bool destroyed);
void voice_push_sim_startup_mode(int mode);
void voice_push_sim_startup_stats(unsigned *out);
void voice_push_sim_connected();
void voice_push_sim_disconnected();
void voice_push_sim_advance(uint64_t ms);
void voice_push_sim_frame(const char *data, unsigned size, unsigned offset, unsigned total, unsigned opcode, bool fin);
void voice_push_sim_refresh();
void voice_push_sim_dismiss();
#endif

// Pure policy: invalid reads, pending-only and revoked journals fail closed.
enum class VoiceBootChoice { Disabled, Legacy, Active };
// Storage failures never select a credential; a later poll may recover from
// transient entropy or allocation failure. Permanent journal faults remain
// fail-closed in the store itself.
inline bool voice_push_boot_retry(bool storage_error) { return storage_error; }
inline VoiceBootChoice voice_push_boot_choice(bool empty, bool ok, bool active, bool candidate, bool tombstone, bool managed_started, bool recovery_required) {
    if (empty) return VoiceBootChoice::Legacy;
    if (!ok || candidate || recovery_required) return VoiceBootChoice::Disabled;
    if (recovery_required) return VoiceBootChoice::Disabled;
    if (active) return VoiceBootChoice::Active;
    if (!candidate && !tombstone && !managed_started) return VoiceBootChoice::Legacy;
    return VoiceBootChoice::Disabled;
}

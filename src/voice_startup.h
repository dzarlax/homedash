#pragma once

// Lifecycle shared by device and simulator. Failed optional voice setup must not
// retain credentials or prevent the independently started network task.
struct VoiceStartupOps {
    bool (*load)();
    bool (*allocate)();
    void (*construct)();
    bool (*mutex)();
    void (*destroy)();
    void (*release)();
    void (*clear_credentials)();
};
inline bool voice_initialize(const VoiceStartupOps &ops) {
    if (!ops.load()) { ops.clear_credentials(); return false; }
    if (!ops.allocate()) { ops.clear_credentials(); return false; }
    ops.construct();
    if (!ops.mutex()) {
        ops.destroy();
        ops.release();
        ops.clear_credentials();
        return false;
    }
    return true;
}
struct VoiceStartOps {
    bool (*ready)();
    bool (*filter_logs)();
    bool (*start_worker)();
    void (*clear_credentials)();
};
inline void voice_start_optional(const VoiceStartOps &ops) {
    if (!ops.ready()) return;
    if (!ops.filter_logs() || !ops.start_worker()) ops.clear_credentials();
}

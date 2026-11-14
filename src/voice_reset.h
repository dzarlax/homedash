#pragma once

struct voice_reset_ops {
    bool (*forget_voice)(void *);
    bool (*clear_preferences)(void *);
    bool (*clear_device)(void *);
    bool (*start_recovery)(void *);
    void (*restore)(void *);
    void *context;
};

enum class voice_reset_result { success, voice_failed, persistence_failed, recovery_failed };
voice_reset_result voice_reset_run(const voice_reset_ops &ops);

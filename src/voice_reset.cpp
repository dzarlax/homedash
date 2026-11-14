#include "voice_reset.h"

voice_reset_result voice_reset_run(const voice_reset_ops &ops) {
    if (!ops.forget_voice(ops.context)) return voice_reset_result::voice_failed;
    if (!ops.clear_preferences(ops.context) || !ops.clear_device(ops.context)) {
        ops.restore(ops.context);
        return voice_reset_result::persistence_failed;
    }
    if (!ops.start_recovery(ops.context)) {
        ops.restore(ops.context);
        return voice_reset_result::recovery_failed;
    }
    return voice_reset_result::success;
}

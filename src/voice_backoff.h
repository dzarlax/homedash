#pragma once
#include <stdint.h>
inline uint32_t voice_reconnect_delay_ms(unsigned seconds, uint32_t random) {
    if (seconds<1) seconds=1;
    if (seconds>=30) return 30000;
    return seconds*1000+(random%1000);
}
inline unsigned voice_reconnect_next(unsigned seconds) {
    if (seconds<1) return 1;
    return seconds>=16 ? 30 : seconds*2;
}

inline unsigned voice_reconnect_after_session(unsigned seconds, bool connected, uint64_t uptime_ms) {
    return connected && uptime_ms>=30000 ? 1 : seconds;
}

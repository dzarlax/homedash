#pragma once
#include "nvs_config.h"
#include "web_sync.h"
#include "voice_pair_runtime.h"
#include <cstring>

namespace voice_pair_policy {
inline bool network_changed(const web_network_update_t &u, const device_config_t &c) {
    return (u.has_wifi_ssid && std::strcmp(u.wifi_ssid, c.wifi_ssid)) ||
        (u.has_wifi_password && std::strcmp(u.wifi_password, c.wifi_pass)) ||
        (u.has_bridge_url && std::strcmp(u.bridge_url, c.bridge_url)) ||
        (u.has_bridge_key && std::strcmp(u.bridge_key, c.bridge_key));
}
inline bool stop_after_poll(voice_pair_runtime::Status before, voice_pair_runtime::Status after) {
    using S = voice_pair_runtime::Status;
    return after == S::Complete || after == S::Failed ||
        (before == S::AwaitingAck && (after == S::Expired || after == S::Cancelled));
}
}

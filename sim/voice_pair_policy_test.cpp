#include "voice_pair_policy.h"
#include <cassert>
#include <cstring>

int main() {
    device_config_t installed{};
    std::strcpy(installed.wifi_ssid, "test-network");
    std::strcpy(installed.wifi_pass, "synthetic-password");
    std::strcpy(installed.bridge_url, "https://bridge.example:443");
    std::strcpy(installed.bridge_key, "synthetic-api-key");
    web_network_update_t repeated{};
    std::strcpy(repeated.wifi_ssid, installed.wifi_ssid);
    std::strcpy(repeated.wifi_password, installed.wifi_pass);
    std::strcpy(repeated.bridge_url, installed.bridge_url);
    std::strcpy(repeated.bridge_key, installed.bridge_key);
    repeated.has_wifi_ssid = repeated.has_wifi_password = repeated.has_bridge_url = repeated.has_bridge_key = true;
    assert(!voice_pair_policy::network_changed(repeated, installed));
    for (int field = 0; field < 4; ++field) {
        auto changed = repeated;
        char *value = field == 0 ? changed.wifi_ssid : field == 1 ? changed.wifi_password :
            field == 2 ? changed.bridge_url : changed.bridge_key;
        value[0] = 'x';
        assert(voice_pair_policy::network_changed(changed, installed));
        changed.has_wifi_ssid = changed.has_wifi_password = changed.has_bridge_url = changed.has_bridge_key = false;
        assert(!voice_pair_policy::network_changed(changed, installed));
    }
    using S = voice_pair_runtime::Status;
    assert(voice_pair_policy::stop_after_poll(S::AwaitingApproval, S::Failed));
    assert(voice_pair_policy::stop_after_poll(S::AwaitingAck, S::Failed));
    assert(voice_pair_policy::stop_after_poll(S::AwaitingAck, S::Cancelled));
    assert(voice_pair_policy::stop_after_poll(S::AwaitingAck, S::Expired));
    assert(voice_pair_policy::stop_after_poll(S::AwaitingAck, S::Complete));
    assert(!voice_pair_policy::stop_after_poll(S::AwaitingApproval, S::Cancelled));
    assert(!voice_pair_policy::stop_after_poll(S::AwaitingAck, S::AwaitingAck));
}

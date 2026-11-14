#pragma once

#include <stdint.h>
#include <string>
#include <array>

// Transport-independent state. Inputs must already be validated against the wire contract.
namespace voice {
enum class Phase { Listening, Processing, Lookup, Answer, ApprovalRequired, Error, Closed };
enum class Playback { Pending, Drained, Unknown };
// Transport metadata only, excluded from snapshot identity.
enum class Delivery { Unknown, Live, Replay };
struct Snapshot {
    std::string lease, session, turn, text;
    uint64_t revision = 0;
    uint32_t ttl_ms = 0;
    Phase phase = Phase::Closed;
    Playback playback = Playback::Unknown;
    bool backend_active = false, text_truncated = false, interrupted = false, active = false;
};
class ViewModel {
public:
    bool apply(const Snapshot &s, uint64_t now, bool blocked, Delivery delivery = Delivery::Unknown);
    bool clear(const std::string &lease, uint64_t revision);
    void disconnect(uint64_t now);
    void connected(uint64_t now);
    void heartbeat(uint64_t now);
    void tick(uint64_t now);
    void touch(uint64_t now);
    void dismiss();
    void block();
    bool visible() const { return visible_; }
    bool offline() const { return offline_; }
    const Snapshot &snapshot() const { return current_; }
private:
    void erase_text();
    Snapshot current_{};
    // Monotonic Bloom filter: false positives suppress a turn; false negatives are impossible.
    std::array<uint8_t, 8192> retired_turns_{};
    std::array<uint8_t, 2048> retired_leases_{};
    bool retired_lease(const std::string &lease, bool add);
    bool retired_turn(const std::string &session, const std::string &turn, bool add);
    std::string dismissed_session_, dismissed_turn_, suppressed_session_, suppressed_turn_;
    uint64_t deadline_ = 0, idle_deadline_ = 0, disconnected_at_ = 0, last_heartbeat_ = 0;
    bool visible_ = false, offline_ = false, disconnected_ = false, has_connection_ = false;
    bool allow_lease_transition_ = true;
};
}

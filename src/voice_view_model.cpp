#include "voice_view_model.h"
#include <algorithm>

namespace voice {
static bool same_content(const Snapshot &a, const Snapshot &b) {
    return a.lease == b.lease && a.session == b.session && a.turn == b.turn &&
        a.revision == b.revision && a.phase == b.phase && a.playback == b.playback &&
        a.backend_active == b.backend_active && a.text == b.text &&
        a.text_truncated == b.text_truncated && a.interrupted == b.interrupted && a.active == b.active;
}
bool ViewModel::retired_lease(const std::string &lease, bool add) {
    uint64_t a=1469598103934665603ULL, b=1099511628211ULL;
    for (unsigned char c: lease) { a=(a^c)*1099511628211ULL; b=(b^c)*0x9e3779b185ebca87ULL; }
    bool present=true;
    for (unsigned i=0;i<4;++i) {
        uint64_t h=a+i*(b|1ULL); h^=h>>33; h*=0xff51afd7ed558ccdULL; h^=h>>33;
        size_t bit=(size_t)(h & 16383); uint8_t mask=(uint8_t)(1u<<(bit&7));
        if (!(retired_leases_[bit>>3]&mask)) present=false;
        if (add) retired_leases_[bit>>3]|=mask;
    }
    return present;
}
bool ViewModel::retired_turn(const std::string &session, const std::string &turn, bool add) {
    uint64_t a=1469598103934665603ULL, b=1099511628211ULL;
    auto feed=[&](unsigned char c) { a=(a^c)*1099511628211ULL; b=(b^c)*0x9e3779b185ebca87ULL; };
    for (unsigned char c: session) feed(c);
    feed(0); for (unsigned char c: turn) feed(c);
    bool present=true;
    for (unsigned i=0;i<4;++i) {
        uint64_t h=a+i*(b|1ULL); h^=h>>33; h*=0xff51afd7ed558ccdULL; h^=h>>33;
        size_t bit=(size_t)(h & 65535); uint8_t mask=(uint8_t)(1u<<(bit&7));
        if (!(retired_turns_[bit>>3]&mask)) present=false;
        if (add) retired_turns_[bit>>3]|=mask;
    }
    return present;
}
void ViewModel::erase_text() { std::fill(current_.text.begin(), current_.text.end(), '\0'); current_.text.clear(); }
bool ViewModel::apply(const Snapshot &s, uint64_t now, bool blocked, Delivery delivery) {
    if (s.lease.empty() || s.session.empty() || s.turn.empty() || !s.revision ||
        !s.ttl_ms || s.ttl_ms > 150000 || s.text.size() > 8192) return false;
    const std::string previous_turn = current_.turn;
    const std::string previous_session = current_.session;
    const bool previous_backend_active = current_.backend_active;
    const bool previous_visible = visible_;
    const bool new_turn = s.turn != previous_turn || s.session != previous_session;
    if (new_turn && retired_turn(s.session, s.turn, false)) return false;
    if (s.lease != current_.lease) {
        // The ordered bridge stream sends a clear before a lease transition. A
        // newly connected stream may start with its current snapshot instead.
        if (retired_lease(s.lease,false) || (!current_.lease.empty() && !allow_lease_transition_)) return false;
        if (!current_.lease.empty()) retired_lease(current_.lease,true);
        erase_text();
        current_ = Snapshot{};
        visible_ = previous_visible; deadline_ = 0;
    }
    allow_lease_transition_ = false;
    if (s.revision < current_.revision) return false;
    if (s.revision == current_.revision) {
        if (!same_content(s, current_) || !deadline_ || now >= deadline_) return false;
        deadline_ = std::min(deadline_, now + s.ttl_ms);
        return true;
    }
    if (new_turn && !previous_turn.empty()) {
        retired_turn(previous_session, previous_turn, true);
    }
    const bool text_changed = s.text != current_.text;
    if (new_turn) {
        visible_ = false;
        // Only an accepted PTT/listening snapshot may initiate an overlay.
        const bool eligible = s.active && s.phase != Phase::Closed &&
            (delivery == Delivery::Live || (delivery == Delivery::Unknown && s.phase == Phase::Listening));
        if (blocked || !eligible) { suppressed_session_=s.session; suppressed_turn_=s.turn; }
        else if (s.session != dismissed_session_ || s.turn != dismissed_turn_) visible_ = true;
    } else if (blocked) {
        suppressed_session_ = s.session; suppressed_turn_ = s.turn;
        visible_ = false;
    }
    erase_text();
    current_ = s;
    deadline_ = now + s.ttl_ms;
    if (new_turn || text_changed || (previous_backend_active && !s.backend_active)) idle_deadline_ = now + 60000;
    if (!s.active || s.phase == Phase::Closed) { visible_ = false; erase_text(); }
    if ((s.session == dismissed_session_ && s.turn == dismissed_turn_) ||
        (s.session == suppressed_session_ && s.turn == suppressed_turn_)) visible_ = false;
    if (!visible_) erase_text();
    offline_ = false;
    return true;
}
bool ViewModel::clear(const std::string &lease, uint64_t revision) {
    if (lease != current_.lease || revision < current_.revision || !revision) return false;
    current_.revision = revision; erase_text(); current_.active = false;
    visible_ = false; deadline_ = idle_deadline_ = 0; allow_lease_transition_ = true;
    return true;
}
void ViewModel::disconnect(uint64_t now) { if (!disconnected_) { disconnected_ = true; disconnected_at_ = now; } }
void ViewModel::connected(uint64_t now) { disconnected_ = false; has_connection_ = true; last_heartbeat_ = now; allow_lease_transition_ = true; }
void ViewModel::heartbeat(uint64_t now) { if (!disconnected_) last_heartbeat_ = now; }
void ViewModel::tick(uint64_t now) {
    if ((disconnected_ && now - disconnected_at_ >= 30000) ||
        (has_connection_ && !disconnected_ && now - last_heartbeat_ >= 30000)) { erase_text(); offline_ = true; suppressed_session_=current_.session; suppressed_turn_ = current_.turn; }
    if (deadline_ && now >= deadline_) { erase_text(); visible_ = false; deadline_ = 0; suppressed_session_=current_.session; suppressed_turn_ = current_.turn; }
    if (visible_ && !current_.backend_active && idle_deadline_ && now >= idle_deadline_) {
        erase_text(); visible_ = false; suppressed_session_=current_.session; suppressed_turn_ = current_.turn;
    }
}
void ViewModel::touch(uint64_t now) { if (visible_) idle_deadline_ = now + 60000; }
void ViewModel::block() { if (!current_.turn.empty()) { suppressed_session_=current_.session; suppressed_turn_ = current_.turn; } visible_ = false; erase_text(); }
void ViewModel::dismiss() { dismissed_session_=current_.session; dismissed_turn_ = current_.turn; visible_ = false; erase_text(); }
}

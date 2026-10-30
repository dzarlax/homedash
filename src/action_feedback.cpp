#include "action_feedback.h"
#include <string.h>
#include <math.h>

static void copy(char *dst, size_t size, const char *src) {
    if (!src) src = "";
    strncpy(dst, src, size - 1); dst[size - 1] = '\0';
}
static bool active(const action_record_t &r) {
    return r.in_flight || ((r.phase == ACTION_PENDING || r.phase == ACTION_ACCEPTED || r.phase == ACTION_UNKNOWN) && !r.timed_out);
}
ActionFeedback::ActionFeedback() : records_{}, next_id_(1) {}
const action_record_t *ActionFeedback::find(const char *entity) const {
    if (!entity) return nullptr;
    for (const auto &r : records_) if (r.phase != ACTION_EMPTY && !strcmp(r.entity, entity)) return &r;
    return nullptr;
}
const action_record_t *ActionFeedback::by_id(uint32_t id) const {
    if (!id) return nullptr;
    for (const auto &r : records_) if (r.phase != ACTION_EMPTY && r.id == id) return &r;
    return nullptr;
}
action_record_t *ActionFeedback::mutable_id(uint32_t id) { return const_cast<action_record_t *>(by_id(id)); }
uint32_t ActionFeedback::begin(const char *entity, const char *action, float target, const char *mode, uint64_t now) {
    if (!entity || !*entity || strlen(entity) >= sizeof(records_[0].entity) || !action || strlen(action) >= sizeof(records_[0].action) || (mode && strlen(mode) >= sizeof(records_[0].mode))) return 0;
    action_record_t *slot = nullptr;
    for (auto &r : records_) if (r.phase != ACTION_EMPTY && !strcmp(r.entity, entity)) { if (active(r)) return 0; slot = &r; break; }
    if (!slot) for (auto &r : records_) if (r.phase == ACTION_EMPTY) { slot = &r; break; }
    if (!slot) for (auto &r : records_) if (!active(r)) { slot = &r; break; }
    if (!slot) return 0;
    *slot = {}; slot->id = next_id_++; if (!next_id_) next_id_ = 1;
    copy(slot->entity, sizeof(slot->entity), entity); copy(slot->action, sizeof(slot->action), action); copy(slot->mode, sizeof(slot->mode), mode);
    slot->target = target; slot->deadline_ms = now + 20000; slot->phase = ACTION_PENDING;
    return slot->id;
}
bool ActionFeedback::dispatch(uint32_t id, uint64_t now) {
    auto *r = mutable_id(id); if (!r || r->phase != ACTION_PENDING || r->in_flight) return false;
    if (now >= r->deadline_ms) { r->phase = ACTION_NOT_SENT; r->timed_out = true; return false; }
    r->in_flight = true; return true;
}
void ActionFeedback::result(uint32_t id, action_phase_t phase, uint64_t now, action_error_t error) {
    auto *r = mutable_id(id); if (!r) return;
    r->in_flight = false; r->error = error;
    if (phase == ACTION_ACCEPTED || phase == ACTION_UNKNOWN || phase == ACTION_REJECTED || phase == ACTION_NOT_SENT) r->phase = phase;
    if (now >= r->deadline_ms) r->timed_out = true;
}
bool ActionFeedback::observe(uint32_t id, const action_observation_t &o, uint64_t now) {
    auto *r = mutable_id(id); if (!r || !o.available || strcmp(r->entity, o.entity) || (r->phase != ACTION_ACCEPTED && r->phase != ACTION_UNKNOWN)) return false;
    bool light = !strcmp(o.kind, "light"), climate = !strcmp(o.kind, "climate");
    if ((light && strncmp(r->entity, "light.", 6)) || (climate && strncmp(r->entity, "climate.", 8))) return false;
    bool on = !strcmp(o.state, "on"), off = !strcmp(o.state, "off");
    bool climate_on = climate && (!strcmp(o.state,"cool") || !strcmp(o.state,"heat") || !strcmp(o.state,"dry") || !strcmp(o.state,"fan_only") || !strcmp(o.state,"auto") || !strcmp(o.state,"heat_cool"));
    bool match = false;
    if (!strcmp(r->action, "turn_on")) match = (light && on) || climate_on;
    else if (!strcmp(r->action, "turn_off")) match = (light || climate) && off;
    else if (!strcmp(r->action, "set_hvac_mode")) match = climate && !strcmp(r->mode,o.state);
    else if (!strcmp(r->action, "set_temperature")) match = climate && o.has_target && fabsf(r->target-o.target) <= 0.1001f;
    if (!match) return false;
    r->phase = r->phase == ACTION_UNKNOWN ? ACTION_UPDATED : ACTION_CONFIRMED; r->cue_until_ms = now + 3000; r->timed_out = false; return true;
}
void ActionFeedback::tick(uint64_t now) {
    for (auto &r : records_) {
        if ((r.phase == ACTION_PENDING || r.phase == ACTION_ACCEPTED || r.phase == ACTION_UNKNOWN) && now >= r.deadline_ms) { r.timed_out = true; if (r.phase == ACTION_PENDING && !r.in_flight) r.phase = ACTION_NOT_SENT; }
        if ((r.phase == ACTION_CONFIRMED || r.phase == ACTION_UPDATED) && now >= r.cue_until_ms) r = {};
    }
}

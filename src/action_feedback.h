#pragma once
#include <stdint.h>
#include <stddef.h>

// Callers serialize access. The tracker stores values only, never LVGL objects.
enum action_error_t { ACTION_ERROR_NONE, ACTION_ERROR_OFFLINE, ACTION_ERROR_QUEUE_FULL, ACTION_ERROR_UNAUTHORIZED, ACTION_ERROR_UNSUPPORTED, ACTION_ERROR_UPSTREAM_UNAVAILABLE, ACTION_ERROR_INVALID_RESPONSE, ACTION_ERROR_TIMEOUT };
enum action_phase_t { ACTION_EMPTY, ACTION_PENDING, ACTION_ACCEPTED, ACTION_UNKNOWN, ACTION_CONFIRMED, ACTION_UPDATED, ACTION_REJECTED, ACTION_NOT_SENT };
struct action_observation_t { char entity[48]; char kind[8]; char state[16]; bool available; bool has_target; float target; };
struct action_record_t {
    uint32_t id;
    char entity[48];
    char action[20];
    char mode[16];
    float target;
    uint64_t deadline_ms;
    uint64_t cue_until_ms;
    action_phase_t phase;
    action_error_t error;
    bool in_flight;
    bool timed_out;
};
class ActionFeedback {
public:
    static const size_t CAPACITY = 9;
    ActionFeedback();
    uint32_t begin(const char *entity, const char *action, float target, const char *mode, uint64_t now_ms);
    bool dispatch(uint32_t id, uint64_t now_ms);
    void result(uint32_t id, action_phase_t phase, uint64_t now_ms, action_error_t error = ACTION_ERROR_NONE);
    bool observe(uint32_t id, const action_observation_t &observation, uint64_t now_ms);
    void tick(uint64_t now_ms);
    const action_record_t *find(const char *entity) const;
    const action_record_t *by_id(uint32_t id) const;
private:
    action_record_t records_[CAPACITY];
    uint32_t next_id_;
    action_record_t *mutable_id(uint32_t id);
};

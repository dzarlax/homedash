#pragma once
#include "bridge.h"
struct action_request_t { uint32_t id; char entity[48]; char action[20]; char mode[16]; float target; };
struct action_executor_hooks_t {
    void *context;
    uint64_t (*now)(void *);
    bool (*dispatch)(void *, uint32_t);
    bridge_action_result_t (*post)(void *, const action_request_t &);
    bridge_action_result_t (*read)(void *, const char *, int);
    void (*publish_post)(void *, uint32_t, const bridge_action_result_t &);
    void (*publish_read)(void *, uint32_t, const bridge_action_result_t &);
    bool (*waiting)(void *, uint32_t, uint64_t *);
};
// Owned only by the network task. The hooks serialize tracker access.
class ActionExecutor {
public:
    explicit ActionExecutor(action_executor_hooks_t hooks);
    void run(const action_request_t &request);
    void poll();
    uint32_t wait_ms(uint32_t maximum) const;
private:
    struct read_t { uint32_t id; char entity[48]; uint64_t due; };
    action_executor_hooks_t hooks_;
    read_t reads_[ActionFeedback::CAPACITY];
    void read_once(uint32_t id, const char *entity, uint64_t deadline);
};

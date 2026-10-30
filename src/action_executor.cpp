#include "action_executor.h"
#include <string.h>
ActionExecutor::ActionExecutor(action_executor_hooks_t hooks) : hooks_(hooks), reads_{} {}
void ActionExecutor::read_once(uint32_t id, const char *entity, uint64_t deadline)
{
    uint64_t now = hooks_.now(hooks_.context); if (now >= deadline) return;
    uint64_t remaining = deadline - now; int timeout = remaining < 3000 ? (int)remaining : 3000;
    auto result = hooks_.read(hooks_.context, entity, timeout);
    hooks_.publish_read(hooks_.context, id, result);
}
void ActionExecutor::run(const action_request_t &request)
{
    if (!hooks_.dispatch(hooks_.context, request.id)) return;
    auto result = hooks_.post(hooks_.context, request);
    hooks_.publish_post(hooks_.context, request.id, result);
    uint64_t deadline = 0;
    if (!hooks_.waiting(hooks_.context, request.id, &deadline)) return;
    read_once(request.id, request.entity, deadline);
    if (!hooks_.waiting(hooks_.context, request.id, &deadline)) return;
    for (auto &read : reads_) if (!read.id) {
        read.id = request.id; strncpy(read.entity, request.entity, sizeof(read.entity)-1);
        read.due = hooks_.now(hooks_.context) + 2000; return;
    }
}
void ActionExecutor::poll()
{
    uint64_t now = hooks_.now(hooks_.context);
    for (auto &read : reads_) if (read.id && now >= read.due) {
        auto item = read; read = {}; uint64_t deadline = 0;
        if (hooks_.waiting(hooks_.context, item.id, &deadline)) read_once(item.id, item.entity, deadline);
    }
}
uint32_t ActionExecutor::wait_ms(uint32_t maximum) const
{
    uint64_t now = hooks_.now(hooks_.context);
    for (const auto &read : reads_) if (read.id) {
        uint64_t wait = read.due > now ? read.due - now : 0;
        if (wait < maximum) maximum = (uint32_t)wait;
    }
    return maximum;
}

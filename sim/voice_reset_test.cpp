#include "voice_reset.h"
#include <cassert>
#include <string>
#include <iostream>

struct fixture { std::string calls; char fail; };
static bool call(void *p, char c) { auto &f = *static_cast<fixture *>(p); f.calls += c; return f.fail != c; }
static bool voice(void *p) { return call(p, 'v'); }
static bool prefs(void *p) { return call(p, 'p'); }
static bool device(void *p) { return call(p, 'd'); }
static bool recovery(void *p) { return call(p, 'a'); }
static void restore(void *p) { call(p, 'r'); }
int main() {
    for (auto scenario : { 'v', 'p', 'd', 'a', '-' }) {
        fixture f = {{}, scenario};
        voice_reset_ops ops = {voice, prefs, device, recovery, restore, &f};
        auto result = voice_reset_run(ops);
        switch (scenario) {
        case 'v': assert(result == voice_reset_result::voice_failed && f.calls == "v"); break;
        case 'p': assert(result == voice_reset_result::persistence_failed && f.calls == "vpr"); break;
        case 'd': assert(result == voice_reset_result::persistence_failed && f.calls == "vpdr"); break;
        case 'a': assert(result == voice_reset_result::recovery_failed && f.calls == "vpdar"); break;
        default: assert(result == voice_reset_result::success && f.calls == "vpda");
        }
    }
    std::cout << "voice reset sequencing and fault injection passed\n";
}

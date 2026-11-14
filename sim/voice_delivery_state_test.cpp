#include "voice_view_model.h"
#include <cassert>
#include <cstdio>
using namespace voice;
static Snapshot make(const char *turn,uint64_t revision,Phase phase) {
    Snapshot s; s.lease="lease"; s.session="session"; s.turn=turn; s.revision=revision;
    s.phase=phase; s.active=true; s.backend_active=true; s.ttl_ms=60000; return s;
}
int main() {
    ViewModel stream;
    auto old=make("turn-1",1,Phase::Listening);
    assert(stream.apply(old,1,false,Delivery::Live) && stream.visible());
    old.revision=2; old.phase=Phase::Answer; old.text="session speech";
    assert(stream.apply(old,2,false,Delivery::Live) && stream.visible());
    // Lost/coalesced turn-2 listening. A live full lookup must interrupt turn 1.
    auto live=make("turn-2",5,Phase::Lookup); live.text="session speech";
    assert(stream.apply(live,3,false,Delivery::Live) && stream.visible());
    assert(stream.snapshot().turn=="turn-2" && stream.snapshot().text=="session speech");
    stream.dismiss(); live.revision=6; live.phase=Phase::Answer;
    assert(stream.apply(live,4,false,Delivery::Live) && !stream.visible());
    // Replay after reconnect is never a new accepted PTT screen takeover.
    ViewModel duplicate; auto d=make("same",1,Phase::Listening); d.ttl_ms=1000;
    assert(duplicate.apply(d,0,false,Delivery::Live));
    d.ttl_ms=500; assert(duplicate.apply(d,100,false,Delivery::Replay) && duplicate.visible());
    duplicate.tick(600); assert(!duplicate.visible());
    assert(!duplicate.apply(d,601,false,Delivery::Replay));
    ViewModel fresh; auto replay=make("replayed",1,Phase::Answer);
    assert(fresh.apply(replay,1,false,Delivery::Replay) && !fresh.visible());
    replay.revision=2; assert(fresh.apply(replay,2,false,Delivery::Live) && !fresh.visible());
    auto blocked=make("blocked",3,Phase::Lookup);
    assert(fresh.apply(blocked,3,true,Delivery::Live) && !fresh.visible());
    blocked.revision=4; assert(fresh.apply(blocked,4,false,Delivery::Live) && !fresh.visible());
    auto later=make("later",5,Phase::Lookup);
    assert(fresh.apply(later,5,false,Delivery::Live) && fresh.visible());
    std::puts("voice live/replay lifecycle passed");
}

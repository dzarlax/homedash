#include "voice_view_model.h"
#include <cassert>
#include <iostream>
using namespace voice;
static Snapshot snap(const char *lease, const char *turn, uint64_t rev) {
    Snapshot s; s.lease=lease; s.session="session"; s.turn=turn; s.revision=rev;
    s.phase=Phase::Listening; s.active=true; s.ttl_ms=150000; return s;
}
int main() {
    ViewModel m; auto a=snap("a","one",1);
    assert(m.apply(a,0,false) && m.visible());
    m.dismiss(); a.revision=2; a.text="private"; a.phase=Phase::Answer;
    assert(m.apply(a,1,false) && !m.visible());
    assert(m.clear("a",2));
    a.lease="b"; a.revision=1; a.phase=Phase::Listening;
    assert(m.apply(a,2,false) && !m.visible());
    auto b=snap("b","two",2); assert(m.apply(b,3,false) && m.visible());
    assert(!m.apply(a,4,false));
    auto late=snap("b","one",3); assert(!m.apply(late,4,false));
    auto old=snap("a","old",99); assert(!m.apply(old,5,false));
    b.revision=3; b.backend_active=true; b.phase=Phase::Lookup;
    assert(m.apply(b,6,false)); m.tick(70000); assert(m.visible());
    b.revision=4; b.backend_active=false; b.phase=Phase::Answer; b.text="тест";
    assert(m.apply(b,70001,false)); m.tick(130000); assert(m.visible());
    m.tick(130001); assert(!m.visible() && m.snapshot().text.empty());
    b.revision=5; assert(m.apply(b,130002,false) && !m.visible());
    auto c=snap("b","three",6); assert(m.apply(c,130003,true) && !m.visible());
    c.revision=7; assert(m.apply(c,130004,false) && !m.visible());
    auto d=snap("b","four",8); assert(m.apply(d,130005,false) && m.visible());
    d.revision=9; d.text="private"; assert(m.apply(d,130006,false));
    m.disconnect(130007); m.tick(160007); assert(m.offline() && m.snapshot().text.empty());
    m.tick(280007); assert(!m.visible());
    ViewModel heartbeat; auto h=snap("heartbeat","turn",1); h.text="private";
    heartbeat.connected(0); assert(heartbeat.apply(h,0,false)); heartbeat.heartbeat(15000);
    heartbeat.tick(44999); assert(!heartbeat.offline()); heartbeat.tick(45000);
    assert(heartbeat.offline() && heartbeat.snapshot().text.empty());
    ViewModel cleared; auto e=snap("clear-lease","clear-turn",1);
    assert(cleared.apply(e,1,false) && cleared.visible());
    assert(cleared.clear("clear-lease",1) && !cleared.visible());
    assert(!cleared.apply(e,2,false));
    ViewModel leases; auto l=snap("lease-0","same-turn",1); assert(leases.apply(l,1,false));
    for (int i=1;i<=100;++i) {
        assert(leases.clear(l.lease,l.revision));
        l.lease="lease-"+std::to_string(i); l.revision=1;
        assert(leases.apply(l,(uint64_t)i+1,false));
        leases.disconnect((uint64_t)i+1); leases.connected((uint64_t)i+1);
        assert(leases.apply(l,(uint64_t)i+1,false));
    }
    ViewModel cycles;
    for (uint64_t i=1;i<=100;++i) {
        auto turn=snap("stable-lease",("ptt-"+std::to_string(i)).c_str(),i*2-1);
        assert(cycles.apply(turn,i*1000,false) && cycles.visible());
        cycles.dismiss(); turn.revision++;
        assert(cycles.apply(turn,i*1000+1,false) && !cycles.visible());
        cycles.disconnect(i*1000+2); cycles.connected(i*1000+3);
        assert(!cycles.visible());
    }
    leases.connected(500); auto old_lease=snap("lease-0","old-turn",999);
    assert(!leases.apply(old_lease,501,false));
    ViewModel reused; auto first=snap("same-lease","same-id",1); first.session="session-a";
    assert(reused.apply(first,1,false)); reused.dismiss();
    auto second=snap("same-lease","same-id",2); second.session="session-b";
    assert(reused.apply(second,2,false) && reused.visible());
    reused.block(); auto third=snap("same-lease","same-id",3); third.session="session-c";
    assert(reused.apply(third,3,false) && reused.visible());
    ViewModel replay; auto r=snap("x","x",1); r.ttl_ms=1000;
    assert(replay.apply(r,0,false)); r.ttl_ms=500; assert(replay.apply(r,100,false));
    replay.tick(600); assert(!replay.visible()); assert(!replay.apply(r,601,false));
    std::cout << "voice state tests passed\n";
}

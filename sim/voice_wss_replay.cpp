#include "voice_fragments.h"
#include "voice_protocol.h"
#include "voice_view_model.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
static uint32_t u32(const unsigned char *p) { return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3]; }
int main() {
    voice::ViewModel model; voice::Fragments fragments; model.connected(1); unsigned accepted=0;
    for (;;) {
        unsigned char h[14]; std::cin.read(reinterpret_cast<char*>(h),sizeof(h));
        if (std::cin.eof()) break;
        assert(std::cin && h[0]<=1); auto total=u32(h+2), off=u32(h+6), n=u32(h+10);
        assert(n<=65536); std::vector<char> data(n); std::cin.read(data.data(),n); assert(std::cin);
        auto r=fragments.add(data.data(),n,off,total,h[0],h[1]);
        if (r!=voice::Fragments::Result::Complete) { assert(r==voice::Fragments::Result::Pending); continue; }
        voice::Message m; assert(voice::parse_message(fragments.message().data(),fragments.message().size(),m)); fragments.reset();
        bool ok=m.clear?model.clear(m.snapshot.lease,m.snapshot.revision):model.apply(m.snapshot,accepted+2,false,m.replay?voice::Delivery::Replay:voice::Delivery::Live);
        if (accepted==0) assert(ok && model.visible());
        if (accepted==1) assert(ok && model.snapshot().text=="Привет");
        if (accepted==2) assert(!ok && model.snapshot().revision==2);
        if (accepted==3) assert(ok && model.visible() && model.snapshot().turn=="turn-2");
        if (accepted==4) assert(ok && !model.visible() && model.snapshot().text.empty());
        ++accepted;
    }
    assert(accepted==5); std::cout << "synthetic WSS replay passed\n";
}

#include "voice_backoff.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main() {
    unsigned seconds=1;
    for(int i=0;i<100;++i) {
        for(unsigned random: {0u,1u,999u,0xffffffffu}) {
            auto delay=voice_reconnect_delay_ms(seconds,random);
            assert(delay>=1000 && delay<=30000);
            if(seconds<30) assert(delay>=seconds*1000 && delay<seconds*1000+1000);
        }
        seconds=voice_reconnect_next(seconds);
    }
    assert(seconds==30); assert(voice_reconnect_next(1)==2);
    assert(voice_reconnect_after_session(16,true,29999)==16);
    assert(voice_reconnect_after_session(16,true,30000)==1);
    assert(voice_reconnect_after_session(16,false,60000)==16);
    std::puts("voice reconnect backoff passed");
}

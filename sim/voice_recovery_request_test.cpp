#include "voice_recovery_request.h"
#include <cassert>
#include <cstring>
#include <iostream>
static bool parse(const char *s, VoiceProvision &p,bool &d) { return voice_recovery_parse(s,strlen(s),p,d); }
int main() {
    VoiceProvision p={}; bool d=false;
    assert(parse("{\"disable\":false,\"base_url\":\"wss://example.test\",\"token\":\"synthetic\"}",p,d));
    assert(!d && !strcmp(p.token,"synthetic"));
    assert(parse("{\"disable\":true,\"base_url\":\"\",\"token\":\"\"}",p,d) && d && !p.token[0]);
    assert(!parse("{\"disable\":false,\"base_url\":\"\",\"token\":\"\"}",p,d));
    assert(!parse("{\"disable\":true,\"base_url\":\"wss://example.test\",\"token\":\"x\"}",p,d));
    assert(!parse("{\"disable\":false,\"base_url\":\"wss://example.test\",\"token\":\"x\",\"token\":\"y\"}",p,d));
    assert(!parse("{\"disable\":false,\"base_url\":\"wss://example.test\",\"token\":\"x\\u0000y\"}",p,d));
    assert(!parse("{\"disable\":false,\"base_url\":\"wss://example.test\",\"token\":\"x\"} garbage",p,d));
    std::cout<<"voice recovery request fixture passed\n";
}

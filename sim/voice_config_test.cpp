#include "voice_config.h"
#include <cassert>
#include <string>
#include <iostream>
int main() {
    assert(voice_config_valid("wss://bridge.example:443","synthetic-token"));
    assert(voice_config_valid("wss://[::1]:443","synthetic-token"));
    assert(voice_config_valid("wss://127.0.0.1","synthetic-token"));
    assert(!voice_config_valid("wss://:","synthetic-token"));
    assert(!voice_config_valid("wss://[]:443","synthetic-token"));
    assert(!voice_config_valid("wss://[bad]:443","synthetic-token"));
    assert(!voice_config_valid("wss://example.test:65536","synthetic-token"));
    assert(!voice_config_valid("wss://example.test:0","synthetic-token"));
    assert(!voice_config_valid("wss://example.test:abc","synthetic-token"));
    assert(!voice_config_valid("wss://999.0.0.1","synthetic-token"));
    assert(!voice_config_valid("wss://-bad.test","synthetic-token"));
    assert(!voice_config_valid("https://bridge.example","synthetic-token"));
    assert(!voice_config_valid("wss://bridge.example/path","synthetic-token"));
    assert(!voice_config_valid("wss://user@bridge.example","synthetic-token"));
    assert(!voice_config_valid("wss://bridge.example?x=y","synthetic-token"));
    assert(!voice_config_valid("wss://bridge.example","bad\r\nAuthorization: forged"));
    assert(!voice_config_valid("wss://bridge.example",""));
    assert(!voice_config_valid("wss://bridge.example",std::string(256,'x').c_str()));
    std::cout<<"voice provisioning validation passed\n";
}

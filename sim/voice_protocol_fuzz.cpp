#include "voice_protocol.h"
#include "voice_fragments.h"
#include <cstddef>
#include <cstdint>
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size) {
    voice::Message message;
    voice::parse_message(reinterpret_cast<const char *>(data),size,message);
    if (size<=65536) {
        voice::Fragments frames; size_t split=size/2;
        frames.add(reinterpret_cast<const char *>(data),split,0,size,1,true);
        if (size>split) frames.add(reinterpret_cast<const char *>(data+split),size-split,split,size,1,true);
    }
    return 0;
}

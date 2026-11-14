#include "voice_fragments.h"
namespace voice {
Fragments::Result Fragments::add(const char *data, size_t size, size_t offset, size_t total, uint8_t opcode, bool fin) {
    if ((!data && size) || total > 65536 || offset > total || size > total-offset || (opcode!=0 && opcode!=1)) { reset(); return Result::Invalid; }
    if (offset==0) {
        if (frame_offset_ != frame_size_ || (opcode==1 && in_message_) || (opcode==0 && !in_message_)) { reset(); return Result::Invalid; }
        if (opcode==1) { buffer_.clear(); in_message_=true; }
        frame_size_=total; frame_offset_=0;
        if (total > 65536-buffer_.size()) { reset(); return Result::Invalid; }
    }
    if (!in_message_ || offset!=frame_offset_ || total!=frame_size_) { reset(); return Result::Invalid; }
    if (size) buffer_.append(data,size);
    frame_offset_+=size;
    if (frame_offset_==frame_size_ && fin) { in_message_=false; return Result::Complete; }
    return Result::Pending;
}
}

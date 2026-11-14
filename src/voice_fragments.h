#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <algorithm>
namespace voice {
class Fragments {
public:
    enum class Result { Pending, Complete, Invalid };
    // opcode is 1 (text) or 0 (continuation). Offset and total describe one WS frame.
    Result add(const char *data, size_t size, size_t offset, size_t total, uint8_t opcode, bool fin);
    const std::string &message() const { return buffer_; }
    void reset() { std::fill(buffer_.begin(), buffer_.end(), '\0'); buffer_.clear(); frame_size_=frame_offset_=0; in_message_=false; }
private:
    std::string buffer_;
    size_t frame_size_=0, frame_offset_=0;
    bool in_message_=false;
};
}

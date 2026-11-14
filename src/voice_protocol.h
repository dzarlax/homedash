#pragma once
#include "voice_view_model.h"
#include <string>
namespace voice {
struct Message { bool clear = false; bool replay = false; Snapshot snapshot; std::string reason; };
// Rejects malformed JSON, invalid UTF-8, duplicate keys and invalid required fields.
bool parse_message(const char *data, size_t size, Message &out);
}

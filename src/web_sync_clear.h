#pragma once
#include "voice_pair_protocol.h"
#include <stddef.h>
struct cJSON;
bool extract_voice_clear(const char *json, size_t length, cJSON *decoded, voice_pair::Clear *out);

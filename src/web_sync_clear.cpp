#include "web_sync_clear.h"
#include "voice_pair_runtime.h"
#include "cJSON.h"
#include <cstring>

// Locate the original top-level object without reserializing signed data.
// The envelope has already passed cJSON validation. Any ambiguous spelling or
// duplicate key is ignored rather than interpreted as a clear command.
bool extract_voice_clear(const char *json, size_t length, cJSON *decoded,
                                voice_pair::Clear *out) {
    unsigned matches = 0;
    for (cJSON *item = decoded->child; item; item = item->next)
        if (item->string && !strcmp(item->string, "voice_clear")) ++matches;
    if (matches != 1 || !json || length < 2 || json[0] != '{') return false;
    size_t i = 1;
    auto ws = [&]() { while (i < length && (json[i]==' ' || json[i]=='\n' || json[i]=='\r' || json[i]=='\t')) ++i; };
    auto string_end = [&]() -> bool {
        if (i >= length || json[i++] != '"') return false;
        while (i < length) {
            if (json[i] == '\\') { i += 2; continue; }
            if (json[i++] == '"') return true;
        }
        return false;
    };
    while (i < length) {
        ws(); if (i >= length || json[i] == '}') break;
        size_t key = i;
        if (!string_end()) return false;
        bool target = i - key == sizeof("\"voice_clear\"") - 1 &&
                      !memcmp(json + key, "\"voice_clear\"", i - key);
        ws(); if (i >= length || json[i++] != ':') return false;
        ws(); size_t start = i; int depth = 0; bool in_string = false;
        while (i < length) {
            char c = json[i];
            if (in_string) {
                if (c == '\\') { i += 2; continue; }
                if (c == '"') in_string = false;
            } else {
                if (c == '"') in_string = true;
                else if (c == '{' || c == '[') ++depth;
                else if (c == '}' || c == ']') { if (depth == 0) break; --depth; }
                else if (c == ',' && depth == 0) break;
            }
            ++i;
        }
        if (target) return i > start && voice_pair_runtime::parse_clear(json + start, i - start, *out);
        if (i >= length || json[i] != ',') break;
        ++i;
    }
    return false;
}

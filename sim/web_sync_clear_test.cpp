#include "../src/web_sync_clear.h"
#include "cJSON.h"
#include <cassert>
#include <cstring>
#include <string>

static std::string captured;
namespace voice_pair_runtime {
bool parse_clear(const char *json, size_t n, voice_pair::Clear &) {
    captured.assign(json, n);
    return captured == "{\"version\":1}";
}
}
static bool extract(const char *json) {
    cJSON *root = cJSON_Parse(json);
    assert(root);
    voice_pair::Clear clear{};
    captured.clear();
    bool ok = extract_voice_clear(json, strlen(json), root, &clear);
    cJSON_Delete(root);
    return ok;
}
int main() {
    assert(extract("{\"nested\":{\"voice_clear\":\"ignored\",\"x\":[1,{\"s\":\"}\\\"\"}]},\"voice_clear\":{\"version\":1},\"tail\":1}"));
    assert(captured == "{\"version\":1}");
    assert(!extract("{\"voice_clear\":{\"version\":1},\"voice_clear\":{\"version\":1}}"));
    assert(!extract("{\"voice_clear\":{\"version\":1},\"voice\\u005fclear\":{\"version\":1}}"));
    assert(!extract("{\"voice\\u005fclear\":{\"version\":1}}"));
    assert(!extract("{\"x\":{\"voice_clear\":{\"version\":1}}}"));
    assert(!extract("{\"voice_clear\":{\"version\":1,\"unknown\":2}}"));
}

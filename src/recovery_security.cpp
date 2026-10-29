#include "recovery_security.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>

namespace {
bool ap_peer(const char *ip) {
    if (!ip || strncmp(ip, "192.168.4.", 10) != 0) return false;
    char *end = nullptr;
    long host = strtol(ip + 10, &end, 10);
    return end && *end == 0 && host >= 2 && host <= 254;
}

bool json_content_type(const char *content_type) {
    static const char type[] = "application/json";
    if (!content_type || strncasecmp(content_type, type, sizeof(type) - 1) != 0) return false;
    const char *suffix = content_type + sizeof(type) - 1;
    return *suffix == 0 || *suffix == ';';
}

bool same_token(const char *provided, const char *expected) {
    if (!provided || !expected) return false;
    size_t a = strlen(provided), b = strlen(expected);
    if (!b || a != b) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a; ++i) diff |= (unsigned char)provided[i] ^ (unsigned char)expected[i];
    return diff == 0;
}
}

bool recovery_request_authorized(const char *local_ip, const char *peer_ip,
                                const char *host, const char *origin,
                                const char *content_type, const char *csrf_token,
                                const char *expected_csrf, bool post_request) {
    if (!local_ip || strcmp(local_ip, "192.168.4.1") || !ap_peer(peer_ip) ||
        !host || strcmp(host, "192.168.4.1")) return false;
    if (origin && origin[0] && strcmp(origin, "http://192.168.4.1")) return false;
    if (!post_request) return true;
    return origin && !strcmp(origin, "http://192.168.4.1") &&
           json_content_type(content_type) && same_token(csrf_token, expected_csrf);
}

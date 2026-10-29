#pragma once

#include <stdbool.h>

// Shared gate for recovery handlers; address values come from the accepted socket.
bool recovery_request_authorized(const char *local_ip, const char *peer_ip,
                                const char *host, const char *origin,
                                const char *content_type, const char *csrf_token,
                                const char *expected_csrf, bool post_request);

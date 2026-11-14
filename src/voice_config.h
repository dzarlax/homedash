#pragma once
#include <cstddef>
#include <cstdint>
// A single NVS blob avoids a mixed URL/token pair on interrupted provisioning.
struct VoiceProvision { uint32_t version; char base_url[160]; char token[256]; };
bool voice_config_valid(const char *base, const char *token);

// Volatile writes keep temporary credentials from being optimized away.
inline void voice_secure_zero(void *ptr, size_t n) {
    volatile unsigned char *p=static_cast<volatile unsigned char *>(ptr);
    while (n--) *p++=0;
}

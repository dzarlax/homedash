#pragma once
#include <stddef.h>
#include <openssl/sha.h>
typedef SHA256_CTX mbedtls_sha256_context;
static inline void mbedtls_sha256_init(mbedtls_sha256_context *) {}
static inline int mbedtls_sha256_starts(mbedtls_sha256_context *c, int) { return SHA256_Init(c) == 1 ? 0 : -1; }
static inline int mbedtls_sha256_update(mbedtls_sha256_context *c, const unsigned char *b, size_t n) { return SHA256_Update(c, b, n) == 1 ? 0 : -1; }
static inline int mbedtls_sha256_finish(mbedtls_sha256_context *c, unsigned char *out) { return SHA256_Final(out, c) == 1 ? 0 : -1; }
static inline void mbedtls_sha256_free(mbedtls_sha256_context *) {}

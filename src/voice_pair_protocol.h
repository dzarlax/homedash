#pragma once
#include <stddef.h>
#include <stdint.h>

// Stateless, bounded wire-format helpers. The caller owns session lifetime,
// verified TLS context, persistent keys and wiping of its own copies.
namespace voice_pair {
constexpr size_t kQRLength = 199;
constexpr size_t kMaxProofLength = 322;
enum class Operation : uint8_t { Deliver = 1, Ack = 2, ClearAck = 3 };
struct Proof {
  Operation operation;
  uint8_t bridge[16], device[16], session[32], challenge[32], generation[16];
  const char *origin;
  size_t origin_len;
  uint64_t epoch;
};
// Accepts only exact canonical HTTPS origin, including explicit port.
bool valid_origin(const char *origin, size_t len);
bool decode_id(const char *hex, size_t len, uint8_t out[16]);
bool encode_id(const uint8_t value[16], char *out, size_t capacity);
// Canonical unpadded base64url; inputs are bounded to 145 binary bytes.
bool encode_base64url(const uint8_t *data, size_t len, char *out, size_t capacity);
bool decode_base64url(const char *encoded, size_t len, uint8_t *out, size_t output_len);
bool proof_bytes(const Proof &proof, uint8_t *out, size_t capacity, size_t *written);
struct QR {
  uint8_t bridge[16], device[16], public_key[65], nonce[32], origin_hash[15];
};
struct Clear {
  uint8_t bridge[16], device[16], device_key_hash[32], generation[16];
  uint64_t epoch;
  uint8_t nonce[32], signature[64];
};
// Caller must obtain these from actual live Wi-Fi and verified clock state,
// and synchronize Wi-Fi state through the single entropy seeding operation.
struct EntropyReady { bool wifi_connected; bool time_valid; };
bool random_secret(const EntropyReady &ready, uint8_t out[32]);
bool random_generation(const EntropyReady &ready, uint8_t out[16]);
bool generate_key(const EntropyReady &ready, uint8_t private_key[32], uint8_t public_key[65]);
bool valid_public_key(const uint8_t *key, size_t len);
bool encode_qr(const QR &qr, const char *origin, size_t origin_len, char *out, size_t capacity);
bool decode_qr(const char *encoded, size_t len, QR *out);
bool qr_matches_origin(const QR &qr, const char *origin, size_t origin_len);
// P1363 low-S signature. The caller must only pass challenges received from
// its own verified HTTPS request and must enforce session lifetime.
bool sign_proof(const Proof &proof, const uint8_t private_key[32], const EntropyReady &ready, uint8_t signature[64]);
// Authenticates a clear_ack challenge request before the server allocates a
// slot. The nonce is generated locally; callers cannot request signing an
// externally supplied nonce. Proof context must come from durable tombstone.
bool sign_clear_ack_preauth(const Proof &context, const uint8_t private_key[32], const EntropyReady &ready, uint8_t client_nonce[32], uint8_t signature[64]);
// Verify the signed command and bind it to locally stored identity, key and
// current generation. Durable epoch/duplicate handling remains the caller's job.
// Hash canonical clear fields plus signature for durable duplicate detection.
bool clear_command_hash(const Clear &command, uint8_t out[32]);
bool verify_clear(const Clear &command, const uint8_t server_key[65], const uint8_t expected_bridge[16], const uint8_t expected_device[16], const uint8_t device_key[65], const uint8_t expected_generation[16]);
void wipe(void *p, size_t n);
}

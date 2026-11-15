#pragma once
#include <stdint.h>
#include <stddef.h>
#include "voice_pair_protocol.h"

// Separate from legacy voice_push. Caller serializes all calls and wipes copies.
// Raw NVS encryption/hardware protection is not assumed.
namespace voice_pair_store {
enum class Result { Ok, Empty, Reset, Rejected, Corrupt, StorageError };
struct Identity {
  uint8_t bridge[16], device[16], private_key[32], public_key[65];
};
struct Credential {
  uint8_t bridge[16], device[16], private_key[32], public_key[65], server_key[65], generation[16];
  uint64_t epoch;
  char wss[160], token[256], https_origin[160];
};
constexpr size_t kMaxRecoveryBindings = 4;
struct State {
  bool has_active, has_candidate, has_tombstone, has_identity, managed_started, clear_acknowledged, recovery_required;
  Identity identity;
  Credential active, candidate; // A tombstoned active retains identity/pin but no access strings.
  size_t recovery_count;
  Credential recovery_bindings[kMaxRecoveryBindings]; // No token/WSS; for signed clear only.
  uint8_t revoked_generation[16], command_hash[32];
  uint64_t epoch;
};
// Loading, preparing, promoting and revoking are fail-closed after an
// ambiguous NVS operation until reboot. The caller must stop using cached
// credentials on any non-Ok result. A missing live Wi-Fi/time precondition
// returns StorageError, without accessing NVS. Caller must synchronize Wi-Fi
// hardware entropy availability for the complete operation.
Result load(State &out, const voice_pair::EntropyReady &ready);
Result prepare(const Credential &candidate, const voice_pair::EntropyReady &ready);
// Persist and read back the signing identity before producing any QR nonce.
// Set replace_existing only for a new owner-initiated physical re-pair; the
// current active credential retains its own signing key. Pending candidates
// must first be resolved; identities for different Bridge/device are rejected.
Result save_identity(const Identity &identity, bool replace_existing, const voice_pair::EntropyReady &ready);
// Discard only after the pairing attempt is cancelled or expired and WSS is
// stopped. It sets a durable recovery_required flag, preventing stale access.
// This never
// changes the active credential; server-side promotion may already require a
// fresh physical pairing if an ACK outcome was ambiguous.
Result discard_candidate(const uint8_t generation[16], const voice_pair::EntropyReady &ready);
// Only call after an authenticated server ACK for exactly this generation.
Result promote(const uint8_t generation[16], const voice_pair::EntropyReady &ready);
// Only call after checking the server signature, IDs, key fingerprint and
// current generation, and closing the local WSS. hash binds the exact command.
Result revoke(const uint8_t generation[16], uint64_t epoch, const uint8_t command_hash[32], const voice_pair::EntropyReady &ready);
// Only after the authenticated clear_ack response, durably mark confirmation.
Result mark_clear_acknowledged(const uint8_t generation[16], uint64_t epoch, const uint8_t command_hash[32], const voice_pair::EntropyReady &ready);
// Offline local reset: durable fence first, then erase and verify both slots.
// A confirmed fence makes load return Reset even when old slots survive a crash.
Result retire_for_reset();
// Call only after the legacy reset step. Independently verifies absence of
// both v2 slots and voice_push/config before removing the reset fence.
Result finish_reset();
void wipe(void *p, size_t n);
}
#ifdef VOICE_PAIR_STORE_TEST
namespace voice_pair_store { void test_reboot(); }
#endif

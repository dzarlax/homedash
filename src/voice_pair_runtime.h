#pragma once
#include "voice_pair_store.h"
#include <stddef.h>
#include <stdint.h>
#include <atomic>

// Coordinator state only; caller serializes all calls on one worker and never
// performs these operations while holding LVGL's lock. No transport consumes
// or signs arbitrary challenges through this interface.
namespace voice_pair_runtime {
enum class Status { Idle, AwaitingApproval, AwaitingAck, Complete, Expired, Cancelled, Failed };
struct Delivery { uint8_t generation[16], server_key[65]; char wss[160], token[256]; };
// Bounded strict server responses. Buffers are wiped on failure.
bool parse_challenge(const char *json,size_t len,uint8_t challenge[32]);
bool parse_delivery(const char *json,size_t len,const char *https_origin,size_t origin_len,Delivery &out);
bool parse_ack(const char *json,size_t len);
// Raw JSON object only; verify its pinned signature before using it.
bool parse_clear(const char *json,size_t len,voice_pair::Clear &out);
// Caller verifies then requests WSS stop; never persist until it reports stopped.
bool verify_clear_command(const voice_pair::Clear &command,const voice_pair::EntropyReady &ready);
bool persist_clear(const voice_pair::Clear &command,bool websocket_stopped,const voice_pair::EntropyReady &ready);
#ifdef ESP_PLATFORM
enum class ClearAckResult { Done, Pending, Error };
// Safe after reboot: reads pinned origin/key and durable tombstone; caller
// requires stopped WSS. Uses local preauthorization and fresh challenge.
ClearAckResult acknowledge_clear(bool websocket_stopped,const voice_pair::EntropyReady &ready);
#endif
class Session {
 public:
  Session();
  ~Session();
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;
  // Must be called only for a physical owner start; IDs and canonical HTTPS
  // origin are frozen for the session. New identity is read back before nonce.
  bool begin(const uint8_t bridge[16],const uint8_t device[16],const char *origin,size_t origin_len,uint64_t monotonic_ms,const voice_pair::EntropyReady &ready,bool recovery_websocket_stopped=false);
  // Caller must invoke frequently enough to hide QR on time and on changes.
  Status tick(const char *current_origin,size_t origin_len,uint64_t monotonic_ms,const voice_pair::EntropyReady &ready);
  bool copy_qr(char *out,size_t capacity,const char *current_origin,size_t origin_len,uint64_t monotonic_ms,const voice_pair::EntropyReady &ready);
#ifdef ESP_PLATFORM
  // Performs one challenge/proof exchange using only verified HTTPS to the
  // frozen origin. Cancel is an atomic UI signal; no external challenge input.
  Status poll(const char *current_origin,size_t origin_len,const std::atomic<bool> &cancel_requested);
#endif
  void cancel();
  Status status() const { return status_; }
 private:
  void clear_secrets();
  Status status_;
  uint64_t started_;
  char origin_[160],qr_[200];
  size_t origin_len_;
  uint8_t nonce_[32], bridge_[16], device_[16];
};
}

#pragma once
#include <atomic>
// UI may request a new session while an old HTTP call is in flight. Starting
// never clears cancellation; only the owning network worker consumes it.
class VoicePairSignals {
 public:
  void start() { start_.store(true,std::memory_order_release); }
  void cancel() { cancel_.store(true,std::memory_order_release); }
  bool take_start() { return start_.exchange(false,std::memory_order_acq_rel); }
  bool take_cancel() { return cancel_.exchange(false,std::memory_order_acq_rel); }
  bool start_pending() const { return start_.load(std::memory_order_acquire); }
  bool should_hide_cancelled() const { return !start_pending(); }
  bool cancelled() const { return cancel_.load(std::memory_order_acquire); }
  const std::atomic<bool> &cancel_flag() const { return cancel_; }
 private:
  std::atomic<bool> start_{false},cancel_{false};
};

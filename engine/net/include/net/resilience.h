#pragma once

// Request coalescing, circuit breaker, and bandwidth hook
// (FR-NET-008 / AC-NET-016).
//
// Coalescer collapses sequential identical GETs to a single fetch; only
// successful (2xx, err == kNone) responses are remembered. Failures are
// never cached. The breaker trips open after N consecutive failures,
// half-opens after the injected-clock timeout with exactly one probe, and
// closes on probe success. ObserveSample is record-only and never throws.

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include "net/retry.h"
#include "net/types.h"

namespace muse {
namespace net {

enum class State {
  kClosed,
  kOpen,
  kHalfOpen,
};

struct CircuitConfig {
  int failureThreshold = 3;
  std::chrono::milliseconds openTimeout{1000};
};

struct BandwidthSample {
  double rttMs = 0.0;
  double bytesPerSec = 0.0;
};

void ObserveSample(const BandwidthSample& s);

class RequestCoalescer {
 public:
  RequestCoalescer() = default;
  Response get(const Request& r, std::function<Response()> fetch);

 private:
  std::unordered_map<std::string, Response> cache_;
};

class CircuitBreaker {
 public:
  explicit CircuitBreaker(CircuitConfig c, IClock& clk);
  bool allow();
  void onSuccess();
  void onFailure();
  State state() const;

 private:
  CircuitConfig config_;
  IClock* clock_;
  int consecutiveFailures_ = 0;
  mutable State state_ = State::kClosed;
  mutable std::uint64_t openedAtMs_ = 0;
  mutable bool probeOutstanding_ = false;
};

}  // namespace net
}  // namespace muse

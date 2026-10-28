#pragma once

// Retry with per-method idempotency rules (FR-NET-008 / AC-NET-014).
//
// Only idempotent requests are retried. Retryable signals are a 503
// response or a transport error (Error::kTransport); any 2xx stops.
// Backoff sleeps are exponential in RetryPolicy::base and use only the
// injected IClock — never a real sleep.

#include <chrono>
#include <cstdint>
#include <functional>

#include "net/types.h"

namespace muse {
namespace net {

// Injected clock seam (contract A-CLK). Tests substitute an in-process
// fake; production passes a wall-clock adapter.
class IClock {
 public:
  virtual ~IClock() = default;
  virtual std::uint64_t nowMs() const = 0;
  virtual void sleepFor(std::chrono::milliseconds d) = 0;
};

struct RetryPolicy {
  int maxAttempts = 0;
  std::chrono::milliseconds base{0};
  double jitter = 0.0;
};

// GET/HEAD/PUT/DELETE/OPTIONS are idempotent; any other method is
// idempotent only when a non-empty Idempotency-Key header (matched
// case-insensitively) is present. Never throws.
bool IsIdempotent(const Request& r);

Response ExecuteWithRetry(const Request& r, RetryPolicy p,
                          std::function<Response(const Request&)> attempt,
                          IClock& clk);

}  // namespace net
}  // namespace muse

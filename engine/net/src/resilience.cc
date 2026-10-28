#include "net/resilience.h"

#include <string>
#include <unordered_map>

namespace muse {
namespace net {
namespace {

// Successful responses are 2xx deliveries; anything else (error status or
// transport failure) is never coalesced.
bool IsSuccessResp(const Response& r) {
  if (r.err != Error::kNone) return false;
  int s = static_cast<int>(r.status);
  return s >= 200 && s < 300;
}

// Coalescing key: method + URL only. Headers/body are deliberately excluded
// so retries with rotated credentials still collapse; hostile bytes (CRLF,
// oversized bodies) stay inert inside the key string.
std::string CacheKey(const Request& r) {
  std::string key;
  key.reserve(r.method.size() + 1 + r.url.size());
  key.append(r.method);
  key.push_back('\x00');
  key.append(r.url);
  return key;
}

// Record-only bandwidth EWMA. No getter; metered-policy deferral is out of
// scope. Updated defensively so hostile/zero samples cannot throw.
double g_ewmaRttMs = 0.0;
double g_ewmaBytesPerSec = 0.0;
bool g_haveSample = false;

std::uint64_t NowMs(const IClock* clk) {
  if (clk == nullptr) return 0;
  try {
    return clk->nowMs();
  } catch (...) {
    return 0;
  }
}

}  // namespace

void ObserveSample(const BandwidthSample& s) {
  try {
    double rtt = s.rttMs;
    double bps = s.bytesPerSec;
    if (!(rtt >= 0.0)) rtt = 0.0;
    if (!(bps >= 0.0)) bps = 0.0;
    constexpr double kAlpha = 0.2;
    if (!g_haveSample) {
      g_ewmaRttMs = rtt;
      g_ewmaBytesPerSec = bps;
      g_haveSample = true;
    } else {
      g_ewmaRttMs += kAlpha * (rtt - g_ewmaRttMs);
      g_ewmaBytesPerSec += kAlpha * (bps - g_ewmaBytesPerSec);
    }
  } catch (...) {
  }
}

Response RequestCoalescer::get(const Request& r, std::function<Response()> fetch) {
  try {
    // Cheap hit path: serve remembered successes without invoking fetch.
    std::string key = CacheKey(r);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;
    Response resp;
    try {
      resp = fetch();
    } catch (...) {
      Response err;
      err.status = static_cast<StatusCode>(0);
      err.err = Error::kTransport;
      return err;
    }
    if (IsSuccessResp(resp)) {
      try {
        cache_[key] = resp;
      } catch (...) {
      }
    }
    return resp;
  } catch (...) {
    // Key construction or map lookup failed; fall back to a direct fetch so
    // hostile inputs still make progress without throwing out of get().
    try {
      return fetch();
    } catch (...) {
      Response err;
      err.status = static_cast<StatusCode>(0);
      err.err = Error::kTransport;
      return err;
    }
  }
}

CircuitBreaker::CircuitBreaker(CircuitConfig c, IClock& clk)
    : config_(c), clock_(&clk) {
  if (config_.failureThreshold < 1) config_.failureThreshold = 1;
}

State CircuitBreaker::state() const {
  try {
    if (state_ == State::kOpen) {
      std::uint64_t timeout =
          static_cast<std::uint64_t>(config_.openTimeout.count() < 0
                                         ? 0
                                         : config_.openTimeout.count());
      if (NowMs(clock_) - openedAtMs_ >= timeout) {
        state_ = State::kHalfOpen;
        probeOutstanding_ = false;
      }
    }
    return state_;
  } catch (...) {
    return state_;
  }
}

bool CircuitBreaker::allow() {
  try {
    State s = state();
    if (s == State::kClosed) return true;
    if (s == State::kOpen) return false;
    // Half-open: exactly one outstanding probe.
    if (probeOutstanding_) return false;
    probeOutstanding_ = true;
    return true;
  } catch (...) {
    return false;
  }
}

void CircuitBreaker::onSuccess() {
  try {
    State s = state();
    if (s == State::kHalfOpen) {
      if (!probeOutstanding_) return;
      probeOutstanding_ = false;
      consecutiveFailures_ = 0;
      state_ = State::kClosed;
      return;
    }
    if (s == State::kOpen) return;
    consecutiveFailures_ = 0;
  } catch (...) {
  }
}

void CircuitBreaker::onFailure() {
  try {
    State s = state();
    if (s == State::kHalfOpen) {
      if (!probeOutstanding_) return;
      probeOutstanding_ = false;
      consecutiveFailures_ = config_.failureThreshold;
      state_ = State::kOpen;
      openedAtMs_ = NowMs(clock_);
      return;
    }
    if (s == State::kOpen) return;
    ++consecutiveFailures_;
    if (consecutiveFailures_ >= config_.failureThreshold) {
      state_ = State::kOpen;
      openedAtMs_ = NowMs(clock_);
    }
  } catch (...) {
  }
}

}  // namespace net
}  // namespace muse

#include "net/resilience.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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

// Coalescing key: method + NUL + url + NUL + hex(FNV-1a64(normalized auth
// material)). Auth material folds in ONLY the first Authorization value and
// the first Cookie value (header name matched case-insensitively, value
// trimmed then compared exact/opaque). Absent and present-but-empty hash
// distinctly (presence flag), so `Authorization: <empty>` never aliases an
// anonymous request. All other headers/body are excluded; hostile bytes stay
// inert inside the key string.
uint64_t Fnv1a64(const std::string& s) {
  uint64_t h = 14695981039346656037ULL;
  for (unsigned char c : s) {
    h ^= static_cast<uint64_t>(c);
    h *= 1099511628211ULL;
  }
  return h;
}

std::string HexU64(uint64_t v) {
  static const char* kHex = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[static_cast<size_t>(i)] = kHex[v & 0xF];
    v >>= 4;
  }
  return out;
}

bool NameEqCi(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    unsigned char ca = static_cast<unsigned char>(a[i]);
    unsigned char cb = static_cast<unsigned char>(b[i]);
    if (std::tolower(ca) != std::tolower(cb)) return false;
  }
  return true;
}

std::string TrimVal(const std::string& s) {
  std::size_t b = 0;
  while (b < s.size() &&
         (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n'))
    ++b;
  std::size_t e = s.size();
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' ||
                   s[e - 1] == '\r' || s[e - 1] == '\n'))
    --e;
  return s.substr(b, e - b);
}

// First value for a header name (case-insensitive); reports presence
// separately so absent vs present-but-empty stay distinct.
std::pair<bool, std::string> FirstAuthValue(
    const std::vector<std::pair<std::string, std::string>>& headers,
    const std::string& name) {
  for (const auto& h : headers) {
    if (NameEqCi(h.first, name)) return {true, TrimVal(h.second)};
  }
  return {false, std::string()};
}

std::string CoalescerKey(const Request& r) {
  auto auth = FirstAuthValue(r.headers, "authorization");
  auto cookie = FirstAuthValue(r.headers, "cookie");
  // Normalized auth material: presence flags keep absent/empty/present
  // distinct; values compared exact/opaque after trimming.
  std::string material;
  material.push_back(auth.first ? '\x01' : '\x00');
  material.append(auth.second);
  material.push_back('\x01');
  material.push_back(cookie.first ? '\x01' : '\x00');
  material.append(cookie.second);
  std::string key;
  key.reserve(r.method.size() + 1 + r.url.size() + 1 + 16);
  key.append(r.method);
  key.push_back('\x00');
  key.append(r.url);
  key.push_back('\x00');
  key.append(HexU64(Fnv1a64(material)));
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

RequestCoalescer::RequestCoalescer(size_t maxEntries)
    : maxEntries_(maxEntries) {}

void RequestCoalescer::clear() {
  try {
    cache_.clear();
  } catch (...) {
  }
}

size_t RequestCoalescer::size() const {
  try {
    return cache_.size();
  } catch (...) {
    return 0;
  }
}

Response RequestCoalescer::get(const Request& r, std::function<Response()> fetch) {
  try {
    // Cheap hit path: serve remembered successes without invoking fetch.
    std::string key = CoalescerKey(r);
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
        // Fail closed on overflow: at capacity → bypass caching
        // (direct fetch, no store, no eviction).
        if (cache_.size() >= maxEntries_) return resp;
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

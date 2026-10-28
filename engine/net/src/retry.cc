#include "net/retry.h"

#include <cctype>
#include <random>
#include <string>

namespace muse {
namespace net {
namespace {

// Header-name comparison is case-insensitive (contract A-HDR). Values are
// copied opaquely and never interpreted, so hostile bytes (CRLF, tokens)
// cannot escape as an exception.
bool NameEq(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

bool MethodEq(const std::string& a, const std::string& b) { return NameEq(a, b); }

bool HasIdempotencyKey(const Request& r) {
  for (const auto& h : r.headers) {
    if (NameEq(h.first, "Idempotency-Key") && !h.second.empty()) return true;
  }
  return false;
}

bool IsRetryable(const Response& r) {
  if (r.err == Error::kTransport) return true;
  if (r.err != Error::kNone) return false;
  return static_cast<int>(r.status) == 503;
}

bool IsSuccess(const Response& r) {
  if (r.err != Error::kNone) return false;
  int s = static_cast<int>(r.status);
  return s >= 200 && s < 300;
}

// Exponential backoff with jitter: base * 2^retryIndex, scaled by a
// uniform factor in [1 - jitter, 1 + jitter] (jitter <= 0 means exact).
// Floored at 1ms so every retry sleeps a positive, non-decreasing span.
std::chrono::milliseconds BackoffFor(const RetryPolicy& p, int retryIndex) {
  double ms = static_cast<double>(p.base.count()) * (1 << retryIndex);
  if (ms < 1.0) ms = 1.0;
  double j = p.jitter;
  if (j < 0.0) j = 0.0;
  if (j > 0.0) {
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<double> dist(-j, j);
    ms *= (1.0 + dist(rng));
    if (ms < 1.0) ms = 1.0;
  }
  return std::chrono::milliseconds(static_cast<long long>(ms));
}

}  // namespace

bool IsIdempotent(const Request& r) {
  if (MethodEq(r.method, "GET") || MethodEq(r.method, "HEAD") ||
      MethodEq(r.method, "PUT") || MethodEq(r.method, "DELETE") ||
      MethodEq(r.method, "OPTIONS"))
    return true;
  return HasIdempotencyKey(r);
}

Response ExecuteWithRetry(const Request& r, RetryPolicy p,
                          std::function<Response(const Request&)> attempt,
                          IClock& clk) {
  int maxAttempts = p.maxAttempts < 1 ? 1 : p.maxAttempts;
  bool idempotent = IsIdempotent(r);
  Response resp = attempt(r);
  int attempts = 1;
  while (attempts < maxAttempts && idempotent && !IsSuccess(resp) &&
         IsRetryable(resp)) {
    clk.sleepFor(BackoffFor(p, attempts - 1));
    resp = attempt(r);
    ++attempts;
  }
  return resp;
}

}  // namespace net
}  // namespace muse

// Batch 3 contract tests — T7 dedup/coalescing, circuit breaker with
// half-open probe, bandwidth estimate hook
// (FR-NET-008 / AC-NET-016 / TC-NET-016).
//
// Hermetic: C++17 + engine/net headers only. The clock is an injected
// in-process fake (no wall-clock, no sleeps, no threads); fetches are
// in-memory lambdas; bandwidth samples are recorded only. No sockets, no
// TLS, no env/config reads, no filesystem, no live services.
//
// Binds to the T7 `Interface:` line in plan.md. Shape ASSUMPTIONS
// (reconciliation rule: update call sites, not expectations, if the coder
// names these differently):
//   A-REQ:  Request/Response are the T1 shapes in net/types.h.
//   A-CODE: StatusCode converts to int; tests use 200/503.
//   A-CLK:  `muse::net::IClock` exposes `uint64_t nowMs() const` and
//           `void sleepFor(std::chrono::milliseconds)`; the tests use a
//           FakeClock with manual advance. Circuit timing reads the clock.
//   A-CFG:  `muse::net::CircuitConfig` is an aggregate of
//           `{ int failureThreshold; std::chrono::milliseconds openTimeout; }`.
//           If the coder adds extra fields with defaults, tests still compile.
//   A-COAL: true thread-concurrent single-flight is not expressible without
//           threads (forbidden in unit tests); the tests pin the closest
//           observable behavior — sequential identical GETs collapse to one
//           fetch, distinct keys fetch independently, and a re-entrant fetch
//           does not deadlock. Genuine N-thread overlap stays TESTER scope.
//   A-BW:   `ObserveSample` records only and never throws; there is no
//           getter to assert on (metered-policy deferral is out of scope).
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "net/resilience.h"
#include "net/types.h"

namespace {

using muse::net::BandwidthSample;
using muse::net::CircuitBreaker;
using muse::net::CircuitConfig;
using muse::net::Error;
using muse::net::IClock;
using muse::net::ObserveSample;
using muse::net::Request;
using muse::net::RequestCoalescer;
using muse::net::Response;
using muse::net::State;
using muse::net::StatusCode;

StatusCode code(int n) { return static_cast<StatusCode>(n); }

Response make_ok(const std::string& body = "ok") {
  Response r;
  r.status = code(200);
  r.body = body;
  r.err = Error::kNone;
  return r;
}

Request make_get(const std::string& url = "https://api.test.invalid/coalesce") {
  Request r;
  r.method = "GET";
  r.url = url;
  r.body = "";
  return r;
}

// Injected fake clock: manual time advance, no wall-clock, no real sleeps.
class FakeClock : public IClock {
 public:
  std::uint64_t nowMs() const override { return now_; }
  void sleepFor(std::chrono::milliseconds d) override {
    now_ += static_cast<std::uint64_t>(d.count());
  }
  void advance(std::chrono::milliseconds d) {
    now_ += static_cast<std::uint64_t>(d.count());
  }

 private:
  std::uint64_t now_ = 0;
};

CircuitConfig cfg3() {
  CircuitConfig c;
  c.failureThreshold = 3;
  c.openTimeout = std::chrono::milliseconds(1000);
  return c;
}

bool check_coalescer_sequential_identical_gets_fetch_once() {
  RequestCoalescer coal;
  int fetches = 0;
  auto fetch = [&]() -> Response {
    ++fetches;
    return make_ok("shared");
  };
  Response a = coal.get(make_get(), fetch);
  Response b = coal.get(make_get(), fetch);
  return fetches == 1 && a.body == "shared" && b.body == "shared";
}

bool check_coalescer_distinct_urls_fetch_independently() {
  RequestCoalescer coal;
  int fetches = 0;
  auto fetch = [&]() -> Response {
    ++fetches;
    return make_ok();
  };
  coal.get(make_get("https://api.test.invalid/a"), fetch);
  coal.get(make_get("https://api.test.invalid/b"), fetch);
  return fetches == 2;
}

bool check_coalescer_distinct_methods_are_not_merged() {
  RequestCoalescer coal;
  int fetches = 0;
  auto fetch = [&]() -> Response {
    ++fetches;
    return make_ok();
  };
  Request get = make_get();
  Request post = make_get();
  post.method = "POST";
  coal.get(get, fetch);
  coal.get(post, fetch);
  return fetches == 2;
}

bool check_coalescer_does_not_cache_failures() {  RequestCoalescer coal;
  int fetches = 0;
  auto fail = [&]() -> Response {
    ++fetches;
    Response e;
    e.status = code(503);
    e.err = Error::kNone;
    return e;
  };
  auto ok = [&]() -> Response {
    ++fetches;
    return make_ok("recovered");
  };
  coal.get(make_get(), fail);
  Response second = coal.get(make_get(), ok);
  return fetches == 2 && second.body == "recovered";
}

bool check_coalescer_key_is_url_only_ignores_authorization() {
  // T7 contract pin (review MINOR): the coalescer key is URL-only per the
  // adjudicated T7 contract — two sequential same-URL GETs with different
  // Authorization headers collapse to one fetch and the second caller
  // receives the first caller's body (rotated credentials do NOT re-fetch).
  RequestCoalescer coal;
  int fetches = 0;
  auto fetch = [&]() -> Response {
    ++fetches;
    return make_ok("first-principal");
  };
  Request first = make_get();
  first.headers.emplace_back("Authorization", "Bearer test-api-key-not-real");
  Request rotated = make_get();
  rotated.headers.emplace_back("Authorization", "Bearer rotated-dummy-key");
  Response a = coal.get(first, fetch);
  Response b = coal.get(rotated, fetch);
  return fetches == 1 && a.body == "first-principal" &&
         b.body == "first-principal";
}

bool check_breaker_starts_closed_and_allows() {
  FakeClock clk;
  CircuitBreaker b(cfg3(), clk);
  return b.state() == State::kClosed && b.allow();
}

bool check_breaker_opens_after_threshold_consecutive_failures() {
  FakeClock clk;
  CircuitBreaker b(cfg3(), clk);
  b.onFailure();
  b.onFailure();
  if (b.state() != State::kClosed) return false;
  b.onFailure();
  return b.state() == State::kOpen && !b.allow();
}

bool check_open_breaker_short_circuits_without_probe() {
  FakeClock clk;
  CircuitBreaker b(cfg3(), clk);
  b.onFailure();
  b.onFailure();
  b.onFailure();
  return b.state() == State::kOpen && !b.allow() && !b.allow();
}

bool check_success_resets_consecutive_failure_count() {
  FakeClock clk;
  CircuitBreaker b(cfg3(), clk);
  b.onFailure();
  b.onFailure();
  b.onSuccess();
  b.onFailure();
  b.onFailure();
  return b.state() == State::kClosed && b.allow();
}

bool check_half_open_after_timeout_allows_exactly_one_probe() {
  FakeClock clk;
  CircuitBreaker b(cfg3(), clk);
  b.onFailure();
  b.onFailure();
  b.onFailure();
  if (b.state() != State::kOpen) return false;
  clk.advance(std::chrono::milliseconds(1000));
  if (b.state() != State::kHalfOpen) return false;
  bool first = b.allow();   // the single probe
  bool second = b.allow();  // must be denied while the probe is outstanding
  return first && !second;
}

bool check_half_open_probe_success_closes_breaker() {
  FakeClock clk;
  CircuitBreaker b(cfg3(), clk);
  b.onFailure();
  b.onFailure();
  b.onFailure();
  clk.advance(std::chrono::milliseconds(1000));
  if (!b.allow()) return false;
  b.onSuccess();
  return b.state() == State::kClosed && b.allow();
}

bool check_half_open_probe_failure_reopens() {
  FakeClock clk;
  CircuitBreaker b(cfg3(), clk);
  b.onFailure();
  b.onFailure();
  b.onFailure();
  clk.advance(std::chrono::milliseconds(1000));
  if (!b.allow()) return false;
  b.onFailure();
  return b.state() == State::kOpen && !b.allow();
}

bool check_open_timeout_not_yet_elapsed_stays_open() {
  FakeClock clk;
  CircuitBreaker b(cfg3(), clk);
  b.onFailure();
  b.onFailure();
  b.onFailure();
  clk.advance(std::chrono::milliseconds(999));
  return b.state() == State::kOpen && !b.allow();
}

bool check_observe_sample_records_without_throwing() {
  BandwidthSample s;
  s.rttMs = 12.5;
  s.bytesPerSec = 1024.0 * 1024.0;
  ObserveSample(s);  // must not throw; records only
  BandwidthSample zero;
  zero.rttMs = 0.0;
  zero.bytesPerSec = 0.0;
  ObserveSample(zero);  // boundary: must not throw
  return true;
}

bool check_hostile_inputs_never_throw() {
  RequestCoalescer coal;
  Request evil = make_get("https://api.test.invalid/\r\ninjected");
  evil.headers.emplace_back("Authorization", "Bearer test-api-key-not-real");
  evil.body = std::string(4096, 'z');
  int fetches = 0;
  Response r = coal.get(evil, [&]() -> Response {
    ++fetches;
    return make_ok();
  });
  FakeClock clk;
  CircuitBreaker b(cfg3(), clk);
  b.onFailure();  // must not throw
  bool ok = b.allow();
  (void)r;
  (void)ok;
  return fetches == 1;
}

}  // namespace

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_coalescer_sequential_identical_gets_fetch_once);
  RUN_CASE(check_coalescer_distinct_urls_fetch_independently);
  RUN_CASE(check_coalescer_distinct_methods_are_not_merged);
  RUN_CASE(check_coalescer_does_not_cache_failures);
  RUN_CASE(check_coalescer_key_is_url_only_ignores_authorization);
  RUN_CASE(check_breaker_starts_closed_and_allows);
  RUN_CASE(check_breaker_opens_after_threshold_consecutive_failures);
  RUN_CASE(check_open_breaker_short_circuits_without_probe);
  RUN_CASE(check_success_resets_consecutive_failure_count);
  RUN_CASE(check_half_open_after_timeout_allows_exactly_one_probe);
  RUN_CASE(check_half_open_probe_success_closes_breaker);
  RUN_CASE(check_half_open_probe_failure_reopens);
  RUN_CASE(check_open_timeout_not_yet_elapsed_stays_open);
  RUN_CASE(check_observe_sample_records_without_throwing);
  RUN_CASE(check_hostile_inputs_never_throw);
  if (failures > 0) {
    std::printf("resilience_test: %d failing\n", failures);
    return 1;
  }
  std::printf("resilience_test: all passing\n");
  return 0;
}

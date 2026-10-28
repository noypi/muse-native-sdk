// Batch 3 contract tests — T6 retry with idempotency keys and per-method rules
// (FR-NET-008 / AC-NET-014 / TC-NET-014).
//
// Hermetic: C++17 + engine/net headers only. The clock is an injected
// in-process fake (no wall-clock, no sleeps); the "server" is an in-memory
// attempt lambda with a fake dedup map; no sockets, no TLS, no env/config
// reads, no filesystem, no live services.
//
// Binds to the T6 `Interface:` line in plan.md. Shape ASSUMPTIONS
// (reconciliation rule: update call sites, not expectations, if the coder
// names these differently):
//   A-REQ:  Request/Response are the T1 shapes in net/types.h (method, url,
//           headers as vector<pair<string,string>>, body; Response carries
//           status/headers/body/err).
//   A-CODE: StatusCode converts to int; tests use 200/503 via
//           static_cast<StatusCode>(n).
//   A-CLK:  `muse::net::IClock` exposes `uint64_t nowMs() const` and
//           `void sleepFor(std::chrono::milliseconds)`; the tests use a
//           FakeClock recording every sleep duration. If the coder names the
//           methods differently (e.g. now()/sleep()), update call sites only.
//   A-RTRY: a 503 response or `err == kTransport` is retryable; any 2xx is
//           success and stops. Backoff sleeps exactly (attempts - 1) times
//           with non-decreasing durations (exponential shape); the tests pin
//           the count and monotonicity, not exact millis (jitter is random).
//   A-KEY:  `IsIdempotent` treats the `Idempotency-Key` header name
//           case-insensitively; presence = non-empty value.
//   A-HDR:  all other header-name comparisons are case-insensitive.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "net/retry.h"
#include "net/types.h"

namespace {

using muse::net::Error;
using muse::net::ExecuteWithRetry;
using muse::net::IClock;
using muse::net::IsIdempotent;
using muse::net::Request;
using muse::net::Response;
using muse::net::RetryPolicy;
using muse::net::StatusCode;

StatusCode code(int n) { return static_cast<StatusCode>(n); }

Response make_status(int status, const std::string& body = "") {
  Response r;
  r.status = code(status);
  r.body = body;
  r.err = Error::kNone;
  return r;
}

Request make_req(const std::string& method, const std::string& url = "https://api.test.invalid/x") {
  Request r;
  r.method = method;
  r.url = url;
  r.body = "";
  return r;
}

Request with_key(Request r, const std::string& key = "key-test-001") {
  r.headers.emplace_back("Idempotency-Key", key);
  return r;
}

// Injected fake clock: scripted nowMs, records every sleepFor duration.
// No wall-clock, no real sleeps.
class FakeClock : public IClock {
 public:
  std::uint64_t nowMs() const override { return now_; }
  void sleepFor(std::chrono::milliseconds d) override {
    sleeps_.push_back(d);
    now_ += static_cast<std::uint64_t>(d.count());
  }
  std::size_t sleepCalls() const { return sleeps_.size(); }
  const std::vector<std::chrono::milliseconds>& sleeps() const { return sleeps_; }

 private:
  std::uint64_t now_ = 0;
  std::vector<std::chrono::milliseconds> sleeps_;
};

RetryPolicy policy3() {
  RetryPolicy p;
  p.maxAttempts = 3;
  p.base = std::chrono::milliseconds(10);
  p.jitter = 0.0;
  return p;
}

bool check_get_is_idempotent() { return IsIdempotent(make_req("GET")); }
bool check_head_is_idempotent() { return IsIdempotent(make_req("HEAD")); }
bool check_put_is_idempotent() { return IsIdempotent(make_req("PUT")); }
bool check_delete_is_idempotent() { return IsIdempotent(make_req("DELETE")); }
bool check_options_is_idempotent() { return IsIdempotent(make_req("OPTIONS")); }
bool check_post_without_key_is_not_idempotent() { return !IsIdempotent(make_req("POST")); }
bool check_patch_without_key_is_not_idempotent() { return !IsIdempotent(make_req("PATCH")); }

bool check_post_with_key_is_idempotent() {
  return IsIdempotent(with_key(make_req("POST")));
}

bool check_patch_with_key_is_idempotent() {
  Request r = make_req("PATCH");
  r.headers.emplace_back("idempotency-key", "k-1");  // lower-case name
  return IsIdempotent(r);
}

bool check_idempotency_key_empty_value_is_absent() {
  Request r = make_req("POST");
  r.headers.emplace_back("Idempotency-Key", "");
  return !IsIdempotent(r);
}

bool check_get_retried_per_policy_then_succeeds() {
  FakeClock clk;
  int calls = 0;
  auto attempt = [&](const Request&) -> Response {
    ++calls;
    if (calls < 3) return make_status(503, "busy");
    return make_status(200, "ok");
  };
  Response r = ExecuteWithRetry(make_req("GET"), policy3(), attempt, clk);
  return calls == 3 && static_cast<int>(r.status) == 200 &&
         r.err == Error::kNone && clk.sleepCalls() == 2;
}

bool check_backoff_sleeps_grow_monotonically() {
  FakeClock clk;
  auto attempt = [&](const Request&) -> Response { return make_status(503); };
  ExecuteWithRetry(make_req("GET"), policy3(), attempt, clk);
  const auto& s = clk.sleeps();
  if (s.size() != 2) return false;
  return s[1].count() >= s[0].count() && s[0].count() > 0;
}

bool check_non_idempotent_post_attempted_exactly_once_on_503() {
  FakeClock clk;
  int calls = 0;
  auto attempt = [&](const Request&) -> Response {
    ++calls;
    return make_status(503, "busy");
  };
  Response r = ExecuteWithRetry(make_req("POST"), policy3(), attempt, clk);
  return calls == 1 && static_cast<int>(r.status) == 503 &&
         clk.sleepCalls() == 0;
}

bool check_post_with_key_retried_per_policy() {
  FakeClock clk;
  int calls = 0;
  auto attempt = [&](const Request&) -> Response {
    ++calls;
    if (calls < 3) return make_status(503, "busy");
    return make_status(200, "created");
  };
  Response r = ExecuteWithRetry(with_key(make_req("POST")), policy3(), attempt, clk);
  return calls == 3 && static_cast<int>(r.status) == 200 &&
         clk.sleepCalls() == 2;
}

bool check_replayed_post_with_same_key_deduped_without_reexecute() {
  FakeClock clk;
  std::unordered_map<std::string, Response> server_log;
  int executions = 0;
  auto server = [&](const Request& q) -> Response {
    std::string key;
    for (const auto& h : q.headers) {
      if (h.first == "Idempotency-Key" || h.first == "idempotency-key") key = h.second;
    }
    auto it = server_log.find(key);
    if (it != server_log.end()) return it->second;  // replay: no re-execute
    ++executions;
    Response ok = make_status(200, "created-once");
    if (!key.empty()) server_log[key] = ok;
    return ok;
  };
  Request q = with_key(make_req("POST"), "dup-key-7");
  Response first = ExecuteWithRetry(q, policy3(), server, clk);
  Response replay = ExecuteWithRetry(q, policy3(), server, clk);
  return executions == 1 && replay.body == "created-once" &&
         first.body == replay.body;
}

bool check_success_first_try_makes_no_sleep() {
  FakeClock clk;
  int calls = 0;
  auto attempt = [&](const Request&) -> Response {
    ++calls;
    return make_status(200, "ok");
  };
  Response r = ExecuteWithRetry(make_req("GET"), policy3(), attempt, clk);
  return calls == 1 && clk.sleepCalls() == 0 && static_cast<int>(r.status) == 200;
}

bool check_transport_error_retried_for_idempotent() {
  FakeClock clk;
  int calls = 0;
  auto attempt = [&](const Request&) -> Response {
    ++calls;
    if (calls == 1) {
      Response e;
      e.status = code(0);
      e.err = Error::kTransport;
      return e;
    }
    return make_status(200, "recovered");
  };
  Response r = ExecuteWithRetry(make_req("GET"), policy3(), attempt, clk);
  return calls == 2 && static_cast<int>(r.status) == 200 && clk.sleepCalls() == 1;
}

bool check_max_attempts_caps_retries() {
  FakeClock clk;
  RetryPolicy p = policy3();
  p.maxAttempts = 1;
  int calls = 0;
  auto attempt = [&](const Request&) -> Response {
    ++calls;
    return make_status(503);
  };
  ExecuteWithRetry(make_req("GET"), p, attempt, clk);
  return calls == 1 && clk.sleepCalls() == 0;
}

bool check_hostile_header_values_never_throw() {
  FakeClock clk;
  Request r = make_req("POST");
  r.headers.emplace_back("X-Evil", "a\r\nB: injected");
  r.headers.emplace_back("Authorization", "Bearer test-api-key-not-real");
  r.body = std::string(1024, 'x');
  bool idem = IsIdempotent(r);  // must not throw
  int calls = 0;
  auto attempt = [&](const Request&) -> Response {
    ++calls;
    return make_status(200, "ok");
  };
  Response out = ExecuteWithRetry(r, policy3(), attempt, clk);  // must not throw
  (void)idem;
  (void)out;
  return calls == 1;  // non-idempotent POST: exactly one attempt
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
  RUN_CASE(check_get_is_idempotent);
  RUN_CASE(check_head_is_idempotent);
  RUN_CASE(check_put_is_idempotent);
  RUN_CASE(check_delete_is_idempotent);
  RUN_CASE(check_options_is_idempotent);
  RUN_CASE(check_post_without_key_is_not_idempotent);
  RUN_CASE(check_patch_without_key_is_not_idempotent);
  RUN_CASE(check_post_with_key_is_idempotent);
  RUN_CASE(check_patch_with_key_is_idempotent);
  RUN_CASE(check_idempotency_key_empty_value_is_absent);
  RUN_CASE(check_get_retried_per_policy_then_succeeds);
  RUN_CASE(check_backoff_sleeps_grow_monotonically);
  RUN_CASE(check_non_idempotent_post_attempted_exactly_once_on_503);
  RUN_CASE(check_post_with_key_retried_per_policy);
  RUN_CASE(check_replayed_post_with_same_key_deduped_without_reexecute);
  RUN_CASE(check_success_first_try_makes_no_sleep);
  RUN_CASE(check_transport_error_retried_for_idempotent);
  RUN_CASE(check_max_attempts_caps_retries);
  RUN_CASE(check_hostile_header_values_never_throw);
  if (failures > 0) {
    std::printf("retry_idempotency_test: %d failing\n", failures);
    return 1;
  }
  std::printf("retry_idempotency_test: all passing\n");
  return 0;
}

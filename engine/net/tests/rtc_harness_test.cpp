// Batch 1 contract tests — T1 (FR-NET-001 / AC-NET-002, TC-NET-002;
// FR-NET-004 seam half / AC-NET-007).
//
// Hermetic: C++17 + engine/net headers only. Transports and the clock are
// in-process fakes defined below; no sockets, no TLS, no env/config reads,
// no filesystem, no live services, no wall-clock sleeps.
//
// Binds to the T1 `Interface:` line in plan.md (ITransport / HttpClient /
// IClock; transport failure -> Response{status 0, err kTransport}, never an
// exception). Type-shape ASSUMPTIONS (reconciliation rule: update call
// sites, not expectations, if the coder names these differently):
//   A-REQ:  Request is default-constructible with assignable `method`,
//           `url` (std::string), `body` (std::string) and `headers`
//           (a vector<pair<string,string>> supporting emplace_back).
//   A-RESP: Response exposes `status`, `body` (std::string), `err`.
//   A-ERR:  `muse::net::Error` has `kTransport` (failure) and `kNone`
//           (success) enumerators; `muse::net::StatusCode` converts to int.
//   A-CTX:  `ExecCtx` and `ClientConfig` are default-constructible.
//   A-CLK:  `muse::net::IClock` is the canonical shape declared once in
//           `net/retry.h` (nowMs + sleepFor, included via `net/transport.h`)
//           per the amended T1 `Interface:` line. FakeClock implements both;
//           sleepFor advances scripted time, never the wall clock.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "net/transport.h"
#include "net/types.h"

namespace {

using muse::net::ClientConfig;
using muse::net::Error;
using muse::net::ExecCtx;
using muse::net::HttpClient;
using muse::net::IClock;
using muse::net::ITransport;
using muse::net::Request;
using muse::net::Response;
using muse::net::StatusCode;

int code_num(StatusCode s) { return static_cast<int>(s); }

Response make_ok(const std::string& body, int status = 200) {
  Response r;
  r.status = static_cast<StatusCode>(status);
  r.body = body;
  r.err = Error::kNone;
  return r;
}

Response make_transport_error() {
  Response r;
  r.status = static_cast<StatusCode>(0);
  r.body = "";
  r.err = Error::kTransport;
  return r;
}

Request make_get(const std::string& url) {
  Request r;
  r.method = "GET";
  r.url = url;
  r.body = "";
  return r;
}

// In-process fake clock: scripted time, manual advance, no wall clock.
// T1 unification: implements the full canonical IClock (nowMs + sleepFor);
// sleepFor advances scripted time (never the wall clock).
class FakeClock : public IClock {
 public:
  explicit FakeClock(std::uint64_t start_ms = 0) : now_(start_ms) {}
  std::uint64_t nowMs() const override { return now_; }
  void sleepFor(std::chrono::milliseconds d) override {
    now_ += static_cast<std::uint64_t>(d.count());
  }
  void advance(std::uint64_t delta_ms) { now_ += delta_ms; }

 private:
  std::uint64_t now_;
};

// In-memory fake transport: records every call with the injected clock's
// timestamp; replays a canned response (or a transport error when failing).
// No I/O of any kind.
class FakeTransport : public ITransport {
 public:
  FakeTransport(std::string id, Response canned, IClock* clk)
      : id_(std::move(id)), canned_(std::move(canned)), clk_(clk) {}

  Response execute(const Request& r, const ExecCtx&) override {
    seen_.push_back(r);
    stamps_.push_back(clk_ != nullptr ? clk_->nowMs() : 0);
    if (failing_) return make_transport_error();
    return canned_;
  }
  std::string name() const override { return id_; }

  void set_failing(bool f) { failing_ = f; }
  std::size_t calls() const { return seen_.size(); }
  const Request& call_at(std::size_t i) const { return seen_.at(i); }
  std::uint64_t stamp_at(std::size_t i) const { return stamps_.at(i); }

 private:
  std::string id_;
  Response canned_;
  IClock* clk_;
  std::vector<Request> seen_;
  std::vector<std::uint64_t> stamps_;
  bool failing_ = false;
};

bool same_request(const Request& a, const Request& b) {
  if (a.method != b.method || a.url != b.url || a.body != b.body) return false;
  if (a.headers.size() != b.headers.size()) return false;
  for (std::size_t i = 0; i < a.headers.size(); ++i) {
    if (a.headers[i].first != b.headers[i].first ||
        a.headers[i].second != b.headers[i].second)
      return false;
  }
  return true;
}

// ---- T1: transport seam reuse (harness proof) ------------------------------

bool check_same_sequence_through_two_transports_is_identical() {
  // Plan T1 acceptance: the same request sequence through two ITransport
  // fakes yields identical calls.
  FakeClock clk;
  std::vector<Request> seq;
  seq.push_back(make_get("https://api.test.invalid/a"));
  Request p;
  p.method = "POST";
  p.url = "https://api.test.invalid/b";
  p.body = "{\"k\":1}";
  seq.push_back(p);
  seq.push_back(make_get("https://api.test.invalid/a"));
  auto t1 = std::make_shared<FakeTransport>("t1", make_ok("x"), &clk);
  auto t2 = std::make_shared<FakeTransport>("t2", make_ok("x"), &clk);
  for (const auto& q : seq) {
    HttpClient c1(t1, ClientConfig{});
    c1.request(q);
    HttpClient c2(t2, ClientConfig{});
    c2.request(q);
  }
  if (t1->calls() != seq.size() || t2->calls() != seq.size()) return false;
  for (std::size_t i = 0; i < seq.size(); ++i) {
    if (!same_request(t1->call_at(i), seq[i])) return false;
    if (!same_request(t2->call_at(i), seq[i])) return false;
  }
  return true;
}

bool check_setTransport_swaps_mid_lifetime() {
  FakeClock clk;
  auto first = std::make_shared<FakeTransport>("first", make_ok("one"), &clk);
  auto second =
      std::make_shared<FakeTransport>("second", make_ok("two"), &clk);
  HttpClient c(first, ClientConfig{});
  Request r = make_get("https://api.test.invalid/v1/x");
  Response a = c.request(r);
  c.setTransport(second);
  Response b = c.request(r);  // identical caller code, new transport
  if (first->calls() != 1 || second->calls() != 1) return false;
  if (a.body != "one" || b.body != "two") return false;
  c.setTransport(first);
  Response d = c.request(r);
  return first->calls() == 2 && d.body == "one";
}

bool check_clock_is_injectable_without_sleeps() {
  // Plan T1 acceptance: IClock is injectable; the test never sleeps.
  FakeClock clk(1000);
  if (clk.nowMs() != 1000) return false;
  clk.advance(250);
  if (clk.nowMs() != 1250) return false;
  clk.advance(0);
  return clk.nowMs() == 1250;
}

bool check_call_timestamps_follow_injected_clock() {
  FakeClock clk(5000);
  auto t = std::make_shared<FakeTransport>("ts", make_ok("ok"), &clk);
  HttpClient c(t, ClientConfig{});
  Request r = make_get("https://api.test.invalid/tick");
  c.request(r);
  clk.advance(40);
  c.request(r);
  clk.advance(60);
  c.request(r);
  if (t->calls() != 3) return false;
  return t->stamp_at(0) == 5000 && t->stamp_at(1) == 5040 &&
         t->stamp_at(2) == 5100;
}

bool check_transport_failure_maps_to_kTransport_without_throwing() {
  FakeClock clk;
  auto t = std::make_shared<FakeTransport>("flaky", make_ok("never"), &clk);
  t->set_failing(true);
  HttpClient c(t, ClientConfig{});
  try {
    Response got = c.request(make_get("https://api.test.invalid/down"));
    if (got.err != Error::kTransport) return false;
    return code_num(got.status) == 0;
  } catch (...) {
    return false;  // error shape must never escape as an exception
  }
}

bool check_null_transport_never_throws() {
  HttpClient c(nullptr, ClientConfig{});
  try {
    Response got = c.request(make_get("https://api.test.invalid/x"));
    return got.err == Error::kTransport && code_num(got.status) == 0;
  } catch (...) {
    return false;
  }
}

bool check_empty_url_is_kTransport_not_exception() {
  FakeClock clk;
  auto t = std::make_shared<FakeTransport>("strict", make_ok("never"), &clk);
  t->set_failing(true);  // models a transport that cannot execute this
  HttpClient c(t, ClientConfig{});
  try {
    Response got = c.request(make_get(""));
    return got.err == Error::kTransport;
  } catch (...) {
    return false;
  }
}

bool check_name_reports_transport_identity() {
  FakeClock clk;
  auto t = std::make_shared<FakeTransport>("boring-fake", make_ok(""), &clk);
  if (t->name() != "boring-fake") return false;
  auto u = std::make_shared<FakeTransport>("cronet-fake", make_ok(""), &clk);
  return u->name() == "cronet-fake";
}

bool check_large_body_round_trips_verbatim() {
  std::string big(1 << 20, 'q');  // 1 MiB boundary payload
  FakeClock clk;
  auto t = std::make_shared<FakeTransport>("bulk", make_ok(big), &clk);
  HttpClient c(t, ClientConfig{});
  Request r = make_get("https://api.test.invalid/big");
  r.body = big;
  Response got = c.request(r);
  if (t->calls() != 1) return false;
  if (t->call_at(0).body.size() != big.size()) return false;
  return got.err == Error::kNone && got.body == big;
}

bool check_header_injection_payload_never_throws() {
  // Security: hostile header bytes (CRLF split attempt) with an obvious
  // dummy credential must not escape as an exception.
  FakeClock clk;
  auto t = std::make_shared<FakeTransport>("sink", make_ok("ok"), &clk);
  HttpClient c(t, ClientConfig{});
  Request r = make_get("https://api.test.invalid/ingest");
  r.headers.emplace_back("Authorization", "Bearer test-api-key-not-real");
  r.headers.emplace_back("x-evil", "a\r\nInjected: yes");
  try {
    Response got = c.request(r);
    if (t->calls() != 1) return false;
    return got.err == Error::kNone;
  } catch (...) {
    return false;
  }
}

bool check_empty_sequence_makes_no_calls() {
  FakeClock clk;
  auto t = std::make_shared<FakeTransport>("idle", make_ok("x"), &clk);
  HttpClient c(t, ClientConfig{});
  (void)c;
  return t->calls() == 0;
}

}  // namespace

#define RUN_CASE(name)                     \
  do {                                     \
    if (!(name)()) {                       \
      std::printf("FAIL %s\n", #name);     \
      ++failures;                          \
    } else {                               \
      std::printf("PASS %s\n", #name);     \
    }                                      \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_same_sequence_through_two_transports_is_identical);
  RUN_CASE(check_setTransport_swaps_mid_lifetime);
  RUN_CASE(check_clock_is_injectable_without_sleeps);
  RUN_CASE(check_call_timestamps_follow_injected_clock);
  RUN_CASE(check_transport_failure_maps_to_kTransport_without_throwing);
  RUN_CASE(check_null_transport_never_throws);
  RUN_CASE(check_empty_url_is_kTransport_not_exception);
  RUN_CASE(check_name_reports_transport_identity);
  RUN_CASE(check_large_body_round_trips_verbatim);
  RUN_CASE(check_header_injection_payload_never_throws);
  RUN_CASE(check_empty_sequence_makes_no_calls);
  if (failures > 0) {
    std::printf("rtc_harness_test: %d failing\n", failures);
    return 1;
  }
  std::printf("rtc_harness_test: all passing\n");
  return 0;
}

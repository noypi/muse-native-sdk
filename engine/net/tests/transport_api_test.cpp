// Batch 1 contract tests — T1 (FR-NET-001 / AC-NET-002 / TC-NET-002) and the
// T2 client-core unit half (FR-NET-004 / AC-NET-007 pool-reuse half).
//
// Hermetic: C++17 + committed engine/net headers only. Transports are
// in-memory fakes defined below; no sockets, no TLS, no env/config reads,
// no filesystem, no live services, no wall-clock sleeps.
//
// Binds to the T1/T2 `Interface:` lines in plan.md. Type-shape ASSUMPTIONS
// (reconciliation rule: update call sites, not expectations, if the coder
// names these differently):
//   A-REQ:  Request is default-constructible with assignable
//           `method`, `url` (std::string), `body` (std::string) and
//           `headers` (a vector<pair<string,string>> supporting emplace_back).
//   A-RESP: Response exposes `status`, `body` (std::string), `err`.
//   A-ERR:  `muse::net::Error` has `kTransport` (failure) and `kNone`
//           (success) enumerators.
//   A-CODE: `muse::net::StatusCode` converts to int for comparison
//           (e.g. static_cast<int>(status) == 200).
//   A-CTX:  `ExecCtx` and `ClientConfig` are default-constructible.
//   A-NEXT: `muse::net::Next` names the "call the rest of the chain"
//           callable used by `Interceptor`
//           (std::function<Response(Request, Next)>); calling next(req)
//           works whether Next takes Request by value or const ref.
//   A-ADD:  interceptors register via the `HttpClient::AddInterceptor`
//           member function, outermost-first execution order.
//   A-POOL: the coder ships a concrete
//           `muse::net::ConnectionPool : public IConnectionPool` with a
//           default constructor; `Origin` is an aggregate of
//           `{scheme, host, port}`; duplicate acquire() on one origin
//           keeps openCount() at 1; release() parks the conn idle
//           (openCount unchanged) for reuse.
//   A-STRM: `HttpClient::requestStream` invokes onChunk >= 1 time and the
//           concatenation of the chunks equals the returned body.
//   A-HTTP: an HTTP error status (e.g. 503) delivered by the transport is
//           a valid response (err == kNone); only failure to execute maps
//           to err == kTransport. `Response::request` and every other
//           public entry point never throw; transport failure is always
//           reported as err == kTransport.
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "net/http_client.h"
#include "net/interceptor.h"
#include "net/transport.h"
#include "net/types.h"

namespace {

using muse::net::Error;
using muse::net::ExecCtx;
using muse::net::HttpClient;
using muse::net::Interceptor;
using muse::net::IConnectionPool;
using muse::net::ITransport;
using muse::net::Next;
using muse::net::Request;
using muse::net::Response;
using muse::net::StatusCode;

StatusCode code(int n) { return static_cast<StatusCode>(n); }
int code_num(StatusCode s) { return static_cast<int>(s); }

Response make_ok(const std::string& body, int status = 200) {
  Response r;
  r.status = code(status);
  r.body = body;
  r.err = Error::kNone;
  return r;
}

Response make_transport_error() {
  Response r;
  r.status = code(0);
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

// In-memory fake transport: records every call, replays a canned response
// (or a transport error when failing_). No I/O of any kind.
class FakeTransport : public ITransport {
 public:
  explicit FakeTransport(std::string id, Response canned)
      : id_(std::move(id)), canned_(std::move(canned)) {}

  Response execute(const Request& r, const ExecCtx&) override {
    seen_.push_back(r);
    if (failing_) return make_transport_error();
    return canned_;
  }
  std::string name() const override { return id_; }

  void set_failing(bool f) { failing_ = f; }
  std::size_t calls() const { return seen_.size(); }
  const Request& call_at(std::size_t i) const { return seen_.at(i); }

 private:
  std::string id_;
  Response canned_;
  std::vector<Request> seen_;
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

// ---- T1: transport-agnostic API -------------------------------------------

bool check_request_delegates_to_bound_transport_verbatim() {
  auto t = std::make_shared<FakeTransport>("fake-default", make_ok("hello"));
  HttpClient c(t, muse::net::ClientConfig{});
  Request r = make_get("https://api.test.invalid/v1/things");
  r.body = "payload";
  r.headers.emplace_back("x-dummy-key", "test-api-key-not-real");
  Response got = c.request(r);
  if (t->calls() != 1) return false;
  if (!same_request(t->call_at(0), r)) return false;
  if (code_num(got.status) != 200 || got.body != "hello") return false;
  return got.err == Error::kNone;
}

bool check_setTransport_swaps_mid_lifetime() {
  auto first = std::make_shared<FakeTransport>("first", make_ok("one"));
  auto second = std::make_shared<FakeTransport>("second", make_ok("two"));
  HttpClient c(first, muse::net::ClientConfig{});
  Request r = make_get("https://api.test.invalid/v1/x");
  Response a = c.request(r);  // caller code path #1
  c.setTransport(second);
  Response b = c.request(r);  // identical caller code path #2
  if (first->calls() != 1 || second->calls() != 1) return false;
  if (a.body != "one" || b.body != "two") return false;
  // Swap back: original transport serves again with no caller change.
  c.setTransport(first);
  Response d = c.request(r);
  return first->calls() == 2 && d.body == "one";
}

bool check_same_sequence_through_two_transports_is_identical() {
  // TC-NET-002 core: the application-visible call sequence must not depend
  // on which transport is bound.
  std::vector<Request> seq;
  Request g = make_get("https://api.test.invalid/a");
  Request p;
  p.method = "POST";
  p.url = "https://api.test.invalid/b";
  p.body = "{\"k\":1}";
  seq.push_back(g);
  seq.push_back(p);
  seq.push_back(g);
  auto run = [&](std::shared_ptr<FakeTransport> t) {
    HttpClient c(t, muse::net::ClientConfig{});
    for (const auto& q : seq) c.request(q);
  };
  auto t1 = std::make_shared<FakeTransport>("t1", make_ok("x"));
  auto t2 = std::make_shared<FakeTransport>("t2", make_ok("x"));
  run(t1);
  run(t2);
  if (t1->calls() != seq.size() || t2->calls() != seq.size()) return false;
  for (std::size_t i = 0; i < seq.size(); ++i) {
    if (!same_request(t1->call_at(i), seq[i])) return false;
    if (!same_request(t2->call_at(i), seq[i])) return false;
  }
  return true;
}

bool check_name_reports_transport_identity() {
  auto t = std::make_shared<FakeTransport>("boring-fake", make_ok(""));
  if (t->name() != "boring-fake") return false;
  auto u = std::make_shared<FakeTransport>("cronet-fake", make_ok(""));
  return u->name() == "cronet-fake";
}

bool check_transport_failure_maps_to_kTransport_without_throwing() {
  auto t = std::make_shared<FakeTransport>("flaky", make_ok("never"));
  t->set_failing(true);
  HttpClient c(t, muse::net::ClientConfig{});
  Response got = c.request(make_get("https://api.test.invalid/down"));
  if (got.err != Error::kTransport) return false;
  return code_num(got.status) == 0 && got.body.empty();
}

bool check_empty_url_is_kTransport_not_exception() {
  auto t = std::make_shared<FakeTransport>("strict", make_ok("never"));
  t->set_failing(true);  // models a transport that cannot execute this
  HttpClient c(t, muse::net::ClientConfig{});
  Request r = make_get("");
  Response got = c.request(r);  // must not throw for any input
  return got.err == Error::kTransport;
}

bool check_http_error_status_is_valid_response_not_transport_error() {
  // A-HTTP: 503 delivered by the transport is data, not a transport fault.
  auto t = std::make_shared<FakeTransport>("svc", make_ok("busy", 503));
  HttpClient c(t, muse::net::ClientConfig{});
  Response got = c.request(make_get("https://api.test.invalid/flaky"));
  return got.err == Error::kNone && code_num(got.status) == 503 &&
         got.body == "busy";
}

bool check_header_injection_payload_never_throws() {
  // Security: hostile header bytes (CRLF split attempt) with an obvious
  // dummy credential must not escape as an exception; the transport fake
  // observes whatever the client forwards.
  auto t = std::make_shared<FakeTransport>("sink", make_ok("ok"));
  HttpClient c(t, muse::net::ClientConfig{});
  Request r = make_get("https://api.test.invalid/ingest");
  r.headers.emplace_back("Authorization", "Bearer test-api-key-not-real");
  r.headers.emplace_back("x-evil", "a\r\nInjected: yes");
  Response got = c.request(r);
  if (t->calls() != 1) return false;
  return got.err == Error::kNone;
}

bool check_large_body_round_trips_verbatim() {
  std::string big(1 << 20, 'q');  // 1 MiB boundary payload
  auto t = std::make_shared<FakeTransport>("bulk", make_ok(big));
  HttpClient c(t, muse::net::ClientConfig{});
  Request r = make_get("https://api.test.invalid/big");
  r.body = big;
  Response got = c.request(r);
  if (t->calls() != 1) return false;
  if (t->call_at(0).body.size() != big.size()) return false;
  return got.err == Error::kNone && got.body == big;
}

// ---- T2: client core (pool reuse, interceptors, streaming) -----------------

bool check_sequential_same_origin_requests_all_succeed() {
  // Unit half of AC-NET-007: sequential same-origin GETs flow through the
  // pooled client path. (Connection-count evidence: pool bookkeeping test
  // below; on-device reuse gate: device-lab/http-dispatch README.)
  auto t = std::make_shared<FakeTransport>("pooled", make_ok("item"));
  HttpClient c(t, muse::net::ClientConfig{});
  for (int i = 0; i < 3; ++i) {
    Response got =
        c.request(make_get("https://api.test.invalid/items?page=1"));
    if (got.err != Error::kNone || code_num(got.status) != 200) return false;
    if (got.body != "item") return false;
  }
  return t->calls() == 3;
}

bool check_interceptors_run_in_registration_order() {
  auto t = std::make_shared<FakeTransport>("chain", make_ok("done"));
  HttpClient c(t, muse::net::ClientConfig{});
  std::vector<std::string> order;
  c.AddInterceptor([&](Request rq, Next next) -> Response {
    order.push_back("inject");
    rq.headers.emplace_back("x-chain-inject", "1");
    return next(rq);
  });
  c.AddInterceptor([&](Request rq, Next next) -> Response {
    order.push_back("recorder");
    bool seen = false;
    for (const auto& h : rq.headers) {
      if (h.first == "x-chain-inject" && h.second == "1") seen = true;
    }
    if (!seen) return make_transport_error();
    return next(rq);
  });
  Response got = c.request(make_get("https://api.test.invalid/chain"));
  if (got.err != Error::kNone) return false;
  if (order.size() != 2 || order[0] != "inject" ||
      order[1] != "recorder")
    return false;
  if (t->calls() != 1) return false;
  // The injected header must reach the transport.
  bool forwarded = false;
  for (const auto& h : t->call_at(0).headers) {
    if (h.first == "x-chain-inject" && h.second == "1") forwarded = true;
  }
  return forwarded;
}

bool check_interceptor_can_short_circuit_without_transport() {
  auto t = std::make_shared<FakeTransport>("origin", make_ok("origin-body"));
  HttpClient c(t, muse::net::ClientConfig{});
  c.AddInterceptor([&](Request, Next) -> Response {
    return make_ok("short-circuit");
  });
  Response got = c.request(make_get("https://api.test.invalid/cached"));
  if (got.err != Error::kNone || got.body != "short-circuit") return false;
  return t->calls() == 0;  // transport never touched
}

bool check_streaming_chunks_arrive_in_order_and_match_body() {
  // A-STRM: chunk concatenation equals the returned body.
  const std::string wire(9000, 's');
  auto t = std::make_shared<FakeTransport>("stream", make_ok(wire));
  HttpClient c(t, muse::net::ClientConfig{});
  std::vector<muse::net::BodyChunk> chunks;
  Response got = c.requestStream(
      make_get("https://api.test.invalid/stream"),
      [&](muse::net::BodyChunk ch) { chunks.push_back(std::move(ch)); });
  if (got.err != Error::kNone) return false;
  if (chunks.empty()) return false;
  std::string joined;
  for (const auto& ch : chunks) joined += ch;
  return joined == got.body && joined == wire;
}

bool check_streaming_empty_body_still_returns_response() {
  auto t = std::make_shared<FakeTransport>("empty-stream", make_ok(""));
  HttpClient c(t, muse::net::ClientConfig{});
  std::size_t callbacks = 0;
  Response got = c.requestStream(
      make_get("https://api.test.invalid/empty"),
      [&](muse::net::BodyChunk) { ++callbacks; });
  (void)callbacks;  // zero or more callbacks both legal for empty bodies
  return got.err == Error::kNone && got.body.empty();
}

bool check_pool_acquire_same_origin_keeps_single_open() {
  // A-POOL: bookkeeping over Origins, no sockets involved.
  muse::net::ConnectionPool pool;
  muse::net::Origin o;
  o.scheme = "https";
  o.host = "api.test.invalid";
  o.port = 443;
  auto c1 = pool.acquire(o);
  auto c2 = pool.acquire(o);
  bool one = (pool.openCount(o) == 1);
  pool.release(std::move(c2));
  pool.release(std::move(c1));
  return one && pool.openCount(o) == 1;  // released conns park idle
}

bool check_pool_tracks_distinct_origins_separately() {
  muse::net::ConnectionPool pool;
  muse::net::Origin a;
  a.scheme = "https";
  a.host = "api.test.invalid";
  a.port = 443;
  muse::net::Origin b;
  b.scheme = "https";
  b.host = "other.test.invalid";
  b.port = 443;
  auto ca = pool.acquire(a);
  auto cb = pool.acquire(b);
  bool ok = (pool.openCount(a) == 1 && pool.openCount(b) == 1);
  pool.release(std::move(ca));
  pool.release(std::move(cb));
  return ok;
}

bool check_pool_reacquire_after_release_reuses_idle() {
  muse::net::ConnectionPool pool;
  muse::net::Origin o;
  o.scheme = "https";
  o.host = "api.test.invalid";
  o.port = 443;
  pool.release(pool.acquire(o));
  auto again = pool.acquire(o);
  bool ok = (pool.openCount(o) == 1);
  pool.release(std::move(again));
  return ok && pool.openCount(o) == 1;
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
  RUN_CASE(check_request_delegates_to_bound_transport_verbatim);
  RUN_CASE(check_setTransport_swaps_mid_lifetime);
  RUN_CASE(check_same_sequence_through_two_transports_is_identical);
  RUN_CASE(check_name_reports_transport_identity);
  RUN_CASE(check_transport_failure_maps_to_kTransport_without_throwing);
  RUN_CASE(check_empty_url_is_kTransport_not_exception);
  RUN_CASE(check_http_error_status_is_valid_response_not_transport_error);
  RUN_CASE(check_header_injection_payload_never_throws);
  RUN_CASE(check_large_body_round_trips_verbatim);
  RUN_CASE(check_sequential_same_origin_requests_all_succeed);
  RUN_CASE(check_interceptors_run_in_registration_order);
  RUN_CASE(check_interceptor_can_short_circuit_without_transport);
  RUN_CASE(check_streaming_chunks_arrive_in_order_and_match_body);
  RUN_CASE(check_streaming_empty_body_still_returns_response);
  RUN_CASE(check_pool_acquire_same_origin_keeps_single_open);
  RUN_CASE(check_pool_tracks_distinct_origins_separately);
  RUN_CASE(check_pool_reacquire_after_release_reuses_idle);
  if (failures > 0) {
    std::printf("transport_api_test: %d failing\n", failures);
    return 1;
  }
  std::printf("transport_api_test: all passing\n");
  return 0;
}

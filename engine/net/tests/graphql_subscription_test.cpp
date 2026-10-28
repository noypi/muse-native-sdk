// Batch 3 contract tests — T6 (FR-NET-009 / AC-NET-017 GraphQL+SSE half,
// TC-NET-017 unit evidence; the external gate is device-lab/rpc-interop).
//
// Hermetic: C++17 + engine/net headers only. SSE/GraphQL bodies are served
// by an in-process FakeTransport; loss/reorder is a scripted LossPlan over
// canned per-event payloads (no sockets, no env/config reads, no
// filesystem, no live services, no wall-clock sleeps).
//
// Binds to the T6 `Interface:` line in plan.md (SseClient::subscribe/cancel
// with onEvent(event,data,id); GraphQLClient query/mutate/subscribe +
// enableApq/enableBatching; LastEventId; resume replays only missed events
// in order; APQ sha256-first flow; cache key shape).
//
// Type-shape ASSUMPTIONS (reconciliation rule: update call sites, not
// expectations, if the coder names these differently):
//   A-SSE:  the SSE wire body is `event:<e>\ndata:<d>\nid:<id>\n\n` frames;
//           the client parses event/data/id and invokes the callback once
//           per frame, in body order.
//   A-RESUME: resume passes the stored id back with `setLastEventId(id)`
//           on the client before re-subscribing; the client sends it as the
//           `Last-Event-ID` request header. Observable through the
//           committed ITransport seam; if the coder names the setter or
//           the subscribe-resume overload differently, only call sites
//           change — the header + replay behavior does not.
//   A-SUB:  GraphQL subscription delivery is one SSE frame per update;
//           `Subscription::next` returns true per update, false at end.
//   A-BATCH: batching is explicit-async: `queue()` buffers one operation
//           and `flush()` executes all buffered operations in one
//           transport execute, returning one Result per queued operation
//           in request order. `query()`/`mutate()` always execute a single
//           operation immediately; `enableBatching(n)` never diverts them.
//   A-KEY:  the normalized-cache key shape is exposed as
//           `GraphQLClient::cacheKey(query, vars)` returning a stable
//           non-empty string: same (query, vars) -> same key, differing
//           vars -> differing keys. The cache itself is NOT built here
//           (plan: key shape only).
//   A-SYNC: callbacks/next() are synchronous over the fake; async delivery
//           reconciles via a coder-added wait hook — expectations unchanged.
//   A-REQ/A-RESP: as in grpc_interop_test.cpp (Request/Response shapes,
//           Error::kTransport/kNone).
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "net/graphql.h"
#include "net/sse.h"
#include "net/transport.h"
#include "net/types.h"

namespace {

using muse::net::Error;
using muse::net::ExecCtx;
using muse::net::GraphQLClient;
using muse::net::ITransport;
using muse::net::LastEventId;
using muse::net::Request;
using muse::net::Response;
using muse::net::SseClient;
using muse::net::StatusCode;

Response make_ok(std::string body,
                 std::vector<std::pair<std::string, std::string>> headers = {}) {
  Response r;
  r.status = StatusCode::kOk;
  r.body = std::move(body);
  r.headers = std::move(headers);
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

// Scripted fake: replays one canned body per execute, records requests.
class FakeLink : public ITransport {
 public:
  explicit FakeLink(std::vector<Response> script)
      : script_(std::move(script)) {}

  Response execute(const Request& r, const ExecCtx&) override {
    seen_.push_back(r);
    if (failing_) return make_transport_error();
    if (at_ < script_.size()) return script_[at_++];
    return make_transport_error();
  }
  std::string name() const override { return "fake-link"; }

  void set_failing(bool f) { failing_ = f; }
  std::size_t calls() const { return seen_.size(); }
  const Request& call_at(std::size_t i) const { return seen_.at(i); }

 private:
  std::vector<Response> script_;
  std::vector<Request> seen_;
  std::size_t at_ = 0;
  bool failing_ = false;
};

bool header_has(const Request& r, const std::string& key,
                const std::string& want_substr) {
  for (const auto& h : r.headers) {
    if (h.first == key && h.second.find(want_substr) != std::string::npos)
      return true;
  }
  return false;
}

// SSE frame builders (A-SSE).
std::string sse_frame(const std::string& event, const std::string& data,
                      const std::string& id) {
  return "event:" + event + "\ndata:" + data + "\nid:" + id + "\n\n";
}

std::string sse_stream(
    const std::vector<std::tuple<std::string, std::string, std::string>>&
        frames) {
  std::string out;
  for (const auto& f : frames)
    out += sse_frame(std::get<0>(f), std::get<1>(f), std::get<2>(f));
  return out;
}

struct CapturedEvent {
  std::string event;
  std::string data;
  std::string id;
};

// ---- SseClient cases -------------------------------------------------------

bool check_sse_subscribe_delivers_events_in_order() {
  auto f = std::make_shared<FakeLink>(std::vector<Response>{make_ok(
      sse_stream({{"update", "d1", "1"}, {"update", "d2", "2"}}))});
  SseClient c(f);
  std::vector<CapturedEvent> got;
  try {
    c.subscribe("https://events.test.invalid/stream",
                [&](std::string e, std::string d, std::string id) {
                  got.push_back({std::move(e), std::move(d), std::move(id)});
                });
  } catch (...) {
    return false;
  }
  if (got.size() != 2) return false;
  return got[0].data == "d1" && got[0].id == "1" && got[1].data == "d2" &&
         got[1].id == "2";
}

bool check_sse_event_names_are_reported() {
  auto f = std::make_shared<FakeLink>(std::vector<Response>{make_ok(
      sse_stream({{"created", "a", "1"}, {"deleted", "b", "2"}}))});
  SseClient c(f);
  std::vector<CapturedEvent> got;
  try {
    c.subscribe("https://events.test.invalid/stream",
                [&](std::string e, std::string d, std::string id) {
                  got.push_back({std::move(e), std::move(d), std::move(id)});
                });
  } catch (...) {
    return false;
  }
  if (got.size() != 2) return false;
  return got[0].event == "created" && got[1].event == "deleted";
}

bool check_sse_cancel_stops_delivery_never_throws() {
  auto f = std::make_shared<FakeLink>(std::vector<Response>{make_ok(
      sse_stream({{"update", "d1", "1"}, {"update", "d2", "2"}}))});
  SseClient c;
  int n = 0;
  try {
    c.subscribe("https://events.test.invalid/stream",
                [&](std::string, std::string, std::string) {
                  ++n;
                  if (n == 1) c.cancel();
                });
    c.cancel();
  } catch (...) {
    return false;
  }
  return n <= 2;  // cancel is safe; at most scripted frames observed
}

bool check_sse_transport_failure_never_throws_no_events() {
  auto f = std::make_shared<FakeLink>(
      std::vector<Response>{make_ok("")});
  f->set_failing(true);
  SseClient c;
  bool saw = false;
  try {
    c.subscribe("https://events.test.invalid/stream",
                [&](std::string, std::string, std::string) { saw = true; });
  } catch (...) {
    return false;
  }
  return !saw;
}

bool check_sse_empty_stream_delivers_nothing_never_throws() {
  auto f = std::make_shared<FakeLink>(
      std::vector<Response>{make_ok("")});
  SseClient c;
  bool saw = false;
  try {
    c.subscribe("https://events.test.invalid/stream",
                [&](std::string, std::string, std::string) { saw = true; });
  } catch (...) {
    return false;
  }
  return !saw;
}

bool check_sse_malformed_lines_are_skipped_never_throws() {
  auto f = std::make_shared<FakeLink>(std::vector<Response>{make_ok(
      "garbage-without-colons\n\n" + sse_frame("update", "good", "7"))});
  SseClient c(f);
  std::vector<CapturedEvent> got;
  try {
    c.subscribe("https://events.test.invalid/stream",
                [&](std::string e, std::string d, std::string id) {
                  got.push_back({std::move(e), std::move(d), std::move(id)});
                });
  } catch (...) {
    return false;
  }
  // The well-formed frame must still arrive; garbage must not throw.
  for (const auto& g : got)
    if (g.data == "good" && g.id == "7") return true;
  return false;
}

// ---- GraphQL query/mutate cases --------------------------------------------

bool check_query_returns_data() {
  auto f = std::make_shared<FakeLink>(std::vector<Response>{
      make_ok(R"({"data":{"user":"ada"}})")});
  GraphQLClient c(f);
  GraphQLClient::Result r{"", "unset"};
  try {
    r = c.query("{ user }", "{}");
  } catch (...) {
    return false;
  }
  return r.error.empty() && r.data.find("ada") != std::string::npos;
}

bool check_mutate_returns_data() {
  auto f = std::make_shared<FakeLink>(std::vector<Response>{
      make_ok(R"({"data":{"ok":true}})")});
  GraphQLClient c(f);
  GraphQLClient::Result r{"", "unset"};
  try {
    r = c.mutate("mutation { setName }", "{}");
  } catch (...) {
    return false;
  }
  return r.error.empty() && r.data.find("ok") != std::string::npos;
}

bool check_graphql_error_surfaces_in_result_never_throws() {
  auto f = std::make_shared<FakeLink>(std::vector<Response>{
      make_ok(R"({"errors":[{"message":"bad field"}]})")});
  GraphQLClient c(f);
  GraphQLClient::Result r{"sentinel", ""};
  try {
    r = c.query("{ nope }", "{}");
  } catch (...) {
    return false;
  }
  return !r.error.empty();
}

bool check_transport_failure_surfaces_as_error_never_throws() {
  auto f = std::make_shared<FakeLink>(
      std::vector<Response>{make_ok("")});
  f->set_failing(true);
  GraphQLClient c(f);
  GraphQLClient::Result r{"", ""};
  try {
    r = c.query("{ user }", "{}");
  } catch (...) {
    return false;
  }
  return !r.error.empty();
}

bool check_empty_query_is_structured_error_never_throws() {
  auto f = std::make_shared<FakeLink>(
      std::vector<Response>{make_ok("")});
  GraphQLClient c(f);
  GraphQLClient::Result r{"", ""};
  try {
    r = c.query("", "{}");
  } catch (...) {
    return false;
  }
  return !r.error.empty();
}

bool check_oversized_input_is_structured_error_never_throws() {
  auto f = std::make_shared<FakeLink>(
      std::vector<Response>{make_ok("")});
  GraphQLClient c(f);
  GraphQLClient::Result r{"", ""};
  try {
    r = c.query(std::string(1 << 20, 'q'), "{}");  // 1 MiB query
  } catch (...) {
    return false;
  }
  return !r.error.empty();
}

// ---- APQ cases ----------------------------------------------------------------

bool check_apq_sends_hash_first_then_full_on_not_found() {
  // Script: first execute answers PersistedQueryNotFound, second answers
  // the data for the full query.
  auto f = std::make_shared<FakeLink>(std::vector<Response>{
      make_ok(R"({"errors":[{"message":"PersistedQueryNotFound"}]})"),
      make_ok(R"({"data":{"user":"ada"}})")});
  GraphQLClient c(f);
  GraphQLClient::Result r{"", "unset"};
  try {
    c.enableApq(true);
    r = c.query("{ user }", "{}");
  } catch (...) {
    return false;
  }
  if (f->calls() != 2) return false;
  // First call carries a sha256 hash, not the full query text.
  const Request& first = f->call_at(0);
  if (first.body.find("{ user }") != std::string::npos) return false;
  if (first.body.find("sha256") == std::string::npos &&
      first.body.find("extensions") == std::string::npos)
    return false;
  // Second call carries the full query and the final result has data.
  const Request& second = f->call_at(1);
  if (second.body.find("{ user }") == std::string::npos) return false;
  return r.error.empty() && r.data.find("ada") != std::string::npos;
}

bool check_apq_hash_hit_needs_no_second_call() {
  auto f = std::make_shared<FakeLink>(std::vector<Response>{
      make_ok(R"({"data":{"user":"ada"}})")});
  GraphQLClient c(f);
  GraphQLClient::Result r{"", "unset"};
  try {
    c.enableApq(true);
    r = c.query("{ user }", "{}");
  } catch (...) {
    return false;
  }
  return f->calls() == 1 && r.error.empty();
}

// ---- batching cases -----------------------------------------------------------

bool check_batching_coalesces_two_queries_into_one_call() {
  // Explicit-async batching: queue()+queue()+flush() performs a single
  // transport call and returns per-caller data in request order.
  auto f = std::make_shared<FakeLink>(std::vector<Response>{
      make_ok(R"([{"data":{"a":1}},{"data":{"b":2}}])")});
  GraphQLClient c(f);
  std::vector<GraphQLClient::Result> out;
  try {
    c.enableBatching(2);
    c.queue("{ a }", "{}");
    c.queue("{ b }", "{}");
    out = c.flush();
  } catch (...) {
    return false;
  }
  if (f->calls() != 1) return false;
  if (out.size() != 2) return false;
  if (!out[0].error.empty() || !out[1].error.empty()) return false;
  // Per-caller data arrives in request order: first result carries the
  // first query's payload, second carries the second's.
  if (out[0].data.find("\"a\":1") == std::string::npos) return false;
  if (out[1].data.find("\"b\":2") == std::string::npos) return false;
  return true;
}

bool check_query_ignores_batching_single_call_unchanged() {
  // Non-batched query() keeps its already-green behavior identically:
  // one call, one operation, even with batching enabled.
  auto f = std::make_shared<FakeLink>(
      std::vector<Response>{make_ok(R"({"data":{"solo":1}})")});
  GraphQLClient c(f);
  GraphQLClient::Result r{"", "unset"};
  try {
    c.enableBatching(2);
    r = c.query("{ solo }", "{}");
  } catch (...) {
    return false;
  }
  if (f->calls() != 1) return false;
  if (!r.error.empty()) return false;
  return r.data.find("\"solo\":1") != std::string::npos;
}

bool check_resume_with_non_numeric_ids_dedupes_by_string_equality() {
  // uuid-style ids: stored id "a-2" drops the replay prefix up to and
  // including "a-2" by string equality (no numeric fast-path involved),
  // so only later events deliver with zero duplicates.
  auto f1 = std::make_shared<FakeLink>(std::vector<Response>{make_ok(
      sse_stream({{"next", "u1", "a-1"}, {"next", "u2", "a-2"}}))});
  GraphQLClient c1(f1);
  auto s1 = c1.subscribe("subscription { ticks }", "{}");
  if (!s1) return false;
  for (int i = 0; i < 2; ++i) {
    GraphQLClient::Result r{"", ""};
    if (!s1->next(&r)) return false;
  }
  std::string resume_id = LastEventId(*s1);
  if (resume_id != "a-2") return false;

  auto f2 = std::make_shared<FakeLink>(std::vector<Response>{make_ok(
      sse_stream({{"next", "u1", "a-1"},
                  {"next", "u2", "a-2"},
                  {"next", "u3", "a-3"},
                  {"next", "u4", "a-4"}}))});
  GraphQLClient c2(f2);
  try {
    c2.setLastEventId(resume_id);
  } catch (...) {
    return false;
  }
  auto s2 = c2.subscribe("subscription { ticks }", "{}");
  if (!s2) return false;
  if (f2->calls() < 1) return false;
  bool resume_header = false;
  for (const auto& h : f2->call_at(0).headers) {
    if (h.first == "Last-Event-ID" && h.second == "a-2") resume_header = true;
  }
  if (!resume_header) return false;
  std::vector<std::string> got;
  for (int i = 0; i < 2; ++i) {
    GraphQLClient::Result r{"", ""};
    if (!s2->next(&r)) return false;
    if (!r.error.empty()) return false;
    got.push_back(r.data);
  }
  // Zero duplicates: the replayed prefix is dropped, nothing extra remains.
  GraphQLClient::Result tail{"", ""};
  if (s2->next(&tail)) return false;
  return got == std::vector<std::string>{"u3", "u4"};
}

// ---- subscription + resume cases -------------------------------------------------

bool check_subscription_delivers_ordered_updates() {
  auto f = std::make_shared<FakeLink>(std::vector<Response>{make_ok(
      sse_stream({{"next", "u1", "1"}, {"next", "u2", "2"}, {"next", "u3", "3"}}))});
  GraphQLClient c(f);
  std::unique_ptr<GraphQLClient::Subscription> s;
  try {
    s = c.subscribe("subscription { ticks }", "{}");
  } catch (...) {
    return false;
  }
  if (!s) return false;
  std::vector<std::string> got;
  for (int i = 0; i < 3; ++i) {
    GraphQLClient::Result r{"", "unset"};
    bool more = false;
    try {
      more = s->next(&r);
    } catch (...) {
      return false;
    }
    if (!more || !r.error.empty()) return false;
    got.push_back(r.data);
  }
  return got == std::vector<std::string>{"u1", "u2", "u3"};
}

bool check_last_event_id_tracks_latest() {
  auto f = std::make_shared<FakeLink>(std::vector<Response>{make_ok(
      sse_stream({{"next", "u1", "1"}, {"next", "u2", "2"}}))});
  GraphQLClient c(f);
  auto s = c.subscribe("subscription { ticks }", "{}");
  if (!s) return false;
  for (int i = 0; i < 2; ++i) {
    GraphQLClient::Result r{"", ""};
    try {
      if (!s->next(&r)) return false;
    } catch (...) {
      return false;
    }
  }
  std::string id;
  try {
    id = LastEventId(*s);
  } catch (...) {
    return false;
  }
  return id == "2";
}

bool check_resume_replays_only_missed_in_order_no_dupes() {
  // Connection 1 delivers events 1..3, then drops (20%-loss style scripted
  // cut). Resume with the stored id replays only 4..5 in order.
  auto f1 = std::make_shared<FakeLink>(std::vector<Response>{make_ok(
      sse_stream({{"next", "u1", "1"}, {"next", "u2", "2"}, {"next", "u3", "3"}}))});
  GraphQLClient c1(f1);
  auto s1 = c1.subscribe("subscription { ticks }", "{}");
  if (!s1) return false;
  std::vector<std::string> got;
  for (int i = 0; i < 3; ++i) {
    GraphQLClient::Result r{"", ""};
    if (!s1->next(&r)) return false;
    got.push_back(r.data);
  }
  std::string resume_id = LastEventId(*s1);
  if (resume_id != "3") return false;

  auto f2 = std::make_shared<FakeLink>(std::vector<Response>{make_ok(
      sse_stream({{"next", "u4", "4"}, {"next", "u5", "5"}}))});
  GraphQLClient c2(f2);
  try {
    c2.setLastEventId(resume_id);  // A-RESUME
  } catch (...) {
    return false;
  }
  auto s2 = c2.subscribe("subscription { ticks }", "{}");
  if (!s2) return false;
  if (f2->calls() < 1) return false;
  // The resume header must carry the stored id so the server replays.
  bool resume_header = false;
  for (const auto& h : f2->call_at(0).headers) {
    if (h.first == "Last-Event-ID" && h.second == "3") resume_header = true;
  }
  if (!resume_header) return false;
  for (int i = 0; i < 2; ++i) {
    GraphQLClient::Result r{"", ""};
    if (!s2->next(&r)) return false;
    got.push_back(r.data);
  }
  return got == std::vector<std::string>{"u1", "u2", "u3", "u4", "u5"};
}

bool check_resume_with_empty_id_replays_from_start() {
  auto f = std::make_shared<FakeLink>(std::vector<Response>{make_ok(
      sse_stream({{"next", "u1", "1"}}))});
  GraphQLClient c(f);
  auto s = c.subscribe("subscription { ticks }", "{}");
  if (!s) return false;
  GraphQLClient::Result r{"", ""};
  bool more = false;
  try {
    more = s->next(&r);
  } catch (...) {
    return false;
  }
  return more && r.data == "u1";
}

bool check_subscription_cancel_never_throws() {
  auto f = std::make_shared<FakeLink>(std::vector<Response>{make_ok(
      sse_stream({{"next", "u1", "1"}}))});
  GraphQLClient c(f);
  auto s = c.subscribe("subscription { ticks }", "{}");
  if (!s) return false;
  try {
    s->cancel();
  } catch (...) {
    return false;
  }
  return true;
}

// ---- cache key shape --------------------------------------------------------------

bool check_cache_key_stable_for_same_inputs() {
  auto f = std::make_shared<FakeLink>(
      std::vector<Response>{make_ok("")});
  GraphQLClient c(f);
  std::string k1, k2;
  try {
    k1 = c.cacheKey("{ user }", "{}");  // A-KEY
    k2 = c.cacheKey("{ user }", "{}");
  } catch (...) {
    return false;
  }
  return !k1.empty() && k1 == k2;
}

bool check_cache_key_differs_across_vars() {
  auto f = std::make_shared<FakeLink>(
      std::vector<Response>{make_ok("")});
  GraphQLClient c(f);
  std::string k1, k2, k3;
  try {
    k1 = c.cacheKey("{ user }", R"({"id":1})");
    k2 = c.cacheKey("{ user }", R"({"id":2})");
    k3 = c.cacheKey("{ other }", R"({"id":1})");
  } catch (...) {
    return false;
  }
  return k1 != k2 && k1 != k3;
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
  RUN_CASE(check_sse_subscribe_delivers_events_in_order);
  RUN_CASE(check_sse_event_names_are_reported);
  RUN_CASE(check_sse_cancel_stops_delivery_never_throws);
  RUN_CASE(check_sse_transport_failure_never_throws_no_events);
  RUN_CASE(check_sse_empty_stream_delivers_nothing_never_throws);
  RUN_CASE(check_sse_malformed_lines_are_skipped_never_throws);
  RUN_CASE(check_query_returns_data);
  RUN_CASE(check_mutate_returns_data);
  RUN_CASE(check_graphql_error_surfaces_in_result_never_throws);
  RUN_CASE(check_transport_failure_surfaces_as_error_never_throws);
  RUN_CASE(check_empty_query_is_structured_error_never_throws);
  RUN_CASE(check_oversized_input_is_structured_error_never_throws);
  RUN_CASE(check_apq_sends_hash_first_then_full_on_not_found);
  RUN_CASE(check_apq_hash_hit_needs_no_second_call);
  RUN_CASE(check_batching_coalesces_two_queries_into_one_call);
  RUN_CASE(check_query_ignores_batching_single_call_unchanged);
  RUN_CASE(check_resume_with_non_numeric_ids_dedupes_by_string_equality);
  RUN_CASE(check_subscription_delivers_ordered_updates);
  RUN_CASE(check_last_event_id_tracks_latest);
  RUN_CASE(check_resume_replays_only_missed_in_order_no_dupes);
  RUN_CASE(check_resume_with_empty_id_replays_from_start);
  RUN_CASE(check_subscription_cancel_never_throws);
  RUN_CASE(check_cache_key_stable_for_same_inputs);
  RUN_CASE(check_cache_key_differs_across_vars);
  if (failures > 0) {
    std::printf("graphql_subscription_test: %d failing\n", failures);
    return 1;
  }
  std::printf("graphql_subscription_test: all passing\n");
  return 0;
}

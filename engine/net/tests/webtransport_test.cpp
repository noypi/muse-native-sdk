// Batch 2 contract tests — T3 (FR-NET-005 / AC-NET-008 WT half,
// RULE-NET-002).
//
// Hermetic: C++17 + engine/net headers only. The HTTP/3 handle is an
// in-process fake scripted below (FakeH3: canned responses + scripted
// Wi-Fi→cellular flap that redelivers in-flight datagrams); no sockets,
// no QUIC, no TLS, no env/config reads, no filesystem, no live services,
// no wall-clock sleeps.
//
// Binds to the T3 `Interface:` line in plan.md (WtState / WebTransport /
// WebTransportConfig / Listener; migration emits kMigrating then
// kConnected; in-flight datagrams neither lost nor duplicated, dedupe by
// stream offset). Type-shape ASSUMPTIONS (reconciliation rule: update call
// sites, not expectations, if the coder names these differently):
//   A-CFG:  `WebTransportConfig` is default-constructible and carries the
//           injected HTTP/3 handle the session runs over (plan Detail:
//           "Session state machine over the ITransport seam (HTTP/3 handle
//           consumed, not implemented)"). The fake scripts the whole
//           session through canned handshake responses.
//   A-SYNC: listener callbacks fire before connect()/sendDatagram()/
//           migrate() return (synchronous delivery). If the coder delivers
//           callbacks on a worker thread instead, the coder adds the
//           wait/poll hook and these call sites gain a wait — expected
//           bytes/state order do not change.
//   A-DEDUP: `sendDatagram(std::string)` carries one stream-0 datagram;
//           the dedupe key is (stream=0, offset=byte position in send
//           order). A redelivered identical payload at an already-seen
//           offset is a duplicate and must be delivered to onDatagram
//           exactly once. Observable purely through the listener.
//   A-REQ:  Request is default-constructible with assignable `method`,
//           `url` (std::string), `body` (std::string) and `headers`
//           (a vector<pair<string,string>> supporting emplace_back).
//   A-RESP: Response exposes `status`, `body` (std::string), `err`;
//           `muse::net::Error` has `kTransport`/`kNone`.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "net/transport.h"
#include "net/types.h"
#include "net/webtransport.h"

namespace {

using muse::net::Error;
using muse::net::ExecCtx;
using muse::net::ITransport;
using muse::net::Request;
using muse::net::Response;
using muse::net::StatusCode;
using muse::net::WebTransport;
using muse::net::WebTransportConfig;
using muse::net::WtState;

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

// Scripted HTTP/3 handle: records requests, replays canned responses.
// set_flap(true) makes the next execute redeliver the last canned body
// (simulating a Wi-Fi→cellular retransmit the session must dedupe).
class FakeH3 : public ITransport {
 public:
  explicit FakeH3(std::vector<Response> script)
      : script_(std::move(script)) {}

  Response execute(const Request& r, const ExecCtx&) override {
    seen_.push_back(r);
    if (failing_) return make_transport_error();
    if (at_ < script_.size()) {
      Response out = script_[at_++];
      if (flap_ && at_ > 1) {
        redelivered_.push_back(script_[at_ - 2].body);
      }
      return out;
    }
    return make_transport_error();
  }
  std::string name() const override { return "fake-h3"; }

  void set_failing(bool f) { failing_ = f; }
  void set_flap(bool f) { flap_ = f; }
  std::size_t calls() const { return seen_.size(); }
  const std::vector<std::string>& redelivered() const { return redelivered_; }

 private:
  std::vector<Response> script_;
  std::vector<Request> seen_;
  std::vector<std::string> redelivered_;
  std::size_t at_ = 0;
  bool failing_ = false;
  bool flap_ = false;
};

struct Captured {
  std::vector<std::pair<WtState, std::string>> states;
  std::vector<std::string> datagrams;
};

typename WebTransport::Listener capture_into(Captured* c) {
  typename WebTransport::Listener l;
  l.onState = [c](WtState s, std::string reason) {
    c->states.emplace_back(s, std::move(reason));
  };
  l.onDatagram = [c](std::string d) { c->datagrams.push_back(std::move(d)); };
  return l;
}

bool state_order_is(const Captured& c,
                    const std::vector<WtState>& want_suffix) {
  if (c.states.size() < want_suffix.size()) return false;
  std::size_t off = c.states.size() - want_suffix.size();
  for (std::size_t i = 0; i < want_suffix.size(); ++i) {
    if (c.states[off + i].first != want_suffix[i]) return false;
  }
  return true;
}

bool last_state_is(const Captured& c, WtState s) {
  return !c.states.empty() && c.states.back().first == s;
}

std::vector<Response> session_script() {
  return {make_ok("session-accept"), make_ok("dg-1"), make_ok("dg-2"),
          make_ok("dg-3")};
}

bool check_connect_emits_connecting_then_connected() {
  auto h3 = std::make_shared<FakeH3>(session_script());
  WebTransportConfig cfg;
  cfg.transport = h3;
  WebTransport wt(cfg);
  Captured c;
  wt.setListener(capture_into(&c));
  try {
    wt.connect("https://edge.test.invalid/session");
  } catch (...) {
    return false;
  }
  return state_order_is(c, {WtState::kConnecting, WtState::kConnected});
}

bool check_migration_emits_migrating_then_connected_in_order() {
  auto h3 = std::make_shared<FakeH3>(session_script());
  WebTransportConfig cfg;
  cfg.transport = h3;
  WebTransport wt(cfg);
  Captured c;
  wt.setListener(capture_into(&c));
  try {
    wt.connect("https://edge.test.invalid/session");
    c.states.clear();
    wt.migrate("cellular");
  } catch (...) {
    return false;
  }
  return state_order_is(c, {WtState::kMigrating, WtState::kConnected});
}

bool check_datagrams_delivered_in_order() {
  auto h3 = std::make_shared<FakeH3>(session_script());
  WebTransportConfig cfg;
  cfg.transport = h3;
  WebTransport wt(cfg);
  Captured c;
  wt.setListener(capture_into(&c));
  try {
    wt.connect("https://edge.test.invalid/session");
    wt.sendDatagram("dg-1");
    wt.sendDatagram("dg-2");
    wt.sendDatagram("dg-3");
  } catch (...) {
    return false;
  }
  if (c.datagrams.size() != 3) return false;
  return c.datagrams[0] == "dg-1" && c.datagrams[1] == "dg-2" &&
         c.datagrams[2] == "dg-3";
}

bool check_inflight_across_flap_neither_lost_nor_duplicated() {
  // Wi-Fi→cellular flap: the handle redelivers in-flight bytes; the
  // session must dedupe by stream offset so the listener still sees
  // each unique datagram exactly once, in order.
  auto h3 = std::make_shared<FakeH3>(session_script());
  WebTransportConfig cfg;
  cfg.transport = h3;
  WebTransport wt(cfg);
  Captured c;
  wt.setListener(capture_into(&c));
  try {
    wt.connect("https://edge.test.invalid/session");
    wt.sendDatagram("dg-1");
    h3->set_flap(true);
    wt.migrate("cellular");
    wt.sendDatagram("dg-2");
    wt.sendDatagram("dg-3");
  } catch (...) {
    return false;
  }
  if (!state_order_is(c, {WtState::kMigrating, WtState::kConnected}))
    return false;
  if (c.datagrams.size() != 3) return false;
  return c.datagrams[0] == "dg-1" && c.datagrams[1] == "dg-2" &&
         c.datagrams[2] == "dg-3";
}

bool check_duplicate_redelivery_deduped() {
  auto h3 = std::make_shared<FakeH3>(session_script());
  WebTransportConfig cfg;
  cfg.transport = h3;
  WebTransport wt(cfg);
  Captured c;
  wt.setListener(capture_into(&c));
  try {
    wt.connect("https://edge.test.invalid/session");
    wt.sendDatagram("dg-1");
    wt.sendDatagram("dg-1");  // retransmit at an already-seen offset
  } catch (...) {
    return false;
  }
  // Exactly-once delivery: the duplicate must not reach the listener.
  if (c.datagrams.size() != 1) return false;
  return c.datagrams[0] == "dg-1";
}

bool check_send_before_connect_reports_kClosed_never_throws() {
  auto h3 = std::make_shared<FakeH3>(session_script());
  WebTransportConfig cfg;
  cfg.transport = h3;
  WebTransport wt(cfg);
  Captured c;
  wt.setListener(capture_into(&c));
  try {
    wt.sendDatagram("early");  // no connect(): must not throw
  } catch (...) {
    return false;
  }
  return last_state_is(c, WtState::kClosed) &&
         !c.states.back().second.empty();
}

bool check_failing_handshake_reports_kClosed_never_throws() {
  auto h3 = std::make_shared<FakeH3>(session_script());
  h3->set_failing(true);
  WebTransportConfig cfg;
  cfg.transport = h3;
  WebTransport wt(cfg);
  Captured c;
  wt.setListener(capture_into(&c));
  try {
    wt.connect("https://edge.test.invalid/session");
  } catch (...) {
    return false;
  }
  return last_state_is(c, WtState::kClosed) &&
         !c.states.back().second.empty();
}

bool check_empty_url_connect_reports_kClosed_never_throws() {
  auto h3 = std::make_shared<FakeH3>(session_script());
  WebTransportConfig cfg;
  cfg.transport = h3;
  WebTransport wt(cfg);
  Captured c;
  wt.setListener(capture_into(&c));
  try {
    wt.connect("");
  } catch (...) {
    return false;
  }
  return last_state_is(c, WtState::kClosed);
}

bool check_migrate_before_connect_reports_kClosed_never_throws() {
  auto h3 = std::make_shared<FakeH3>(session_script());
  WebTransportConfig cfg;
  cfg.transport = h3;
  WebTransport wt(cfg);
  Captured c;
  wt.setListener(capture_into(&c));
  try {
    wt.migrate("cellular");  // no session: must not throw
  } catch (...) {
    return false;
  }
  return last_state_is(c, WtState::kClosed) &&
         !c.states.back().second.empty();
}

bool check_double_migrate_ends_connected_never_throws() {
  auto h3 = std::make_shared<FakeH3>(session_script());
  WebTransportConfig cfg;
  cfg.transport = h3;
  WebTransport wt(cfg);
  Captured c;
  wt.setListener(capture_into(&c));
  try {
    wt.connect("https://edge.test.invalid/session");
    wt.migrate("cellular");
    wt.migrate("wifi");
  } catch (...) {
    return false;
  }
  return last_state_is(c, WtState::kConnected);
}

bool check_large_and_empty_datagrams_round_trip() {
  std::string big(1 << 20, 'd');  // 1 MiB boundary payload
  std::vector<Response> script = {make_ok("session-accept"), make_ok(big),
                                  make_ok("")};
  auto h3 = std::make_shared<FakeH3>(std::move(script));
  WebTransportConfig cfg;
  cfg.transport = h3;
  WebTransport wt(cfg);
  Captured c;
  wt.setListener(capture_into(&c));
  try {
    wt.connect("https://edge.test.invalid/session");
    wt.sendDatagram(big);
    wt.sendDatagram("");
  } catch (...) {
    return false;
  }
  if (c.datagrams.size() != 2) return false;
  return c.datagrams[0] == big && c.datagrams[1].empty();
}

bool check_send_after_close_reports_kClosed_never_throws() {
  auto h3 = std::make_shared<FakeH3>(session_script());
  h3->set_failing(true);  // force a terminal failure first
  WebTransportConfig cfg;
  cfg.transport = h3;
  WebTransport wt(cfg);
  Captured c;
  wt.setListener(capture_into(&c));
  try {
    wt.connect("https://edge.test.invalid/session");
    if (!last_state_is(c, WtState::kClosed)) return false;
    wt.sendDatagram("late");  // after close: must not throw
  } catch (...) {
    return false;
  }
  return last_state_is(c, WtState::kClosed);
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
  RUN_CASE(check_connect_emits_connecting_then_connected);
  RUN_CASE(check_migration_emits_migrating_then_connected_in_order);
  RUN_CASE(check_datagrams_delivered_in_order);
  RUN_CASE(check_inflight_across_flap_neither_lost_nor_duplicated);
  RUN_CASE(check_duplicate_redelivery_deduped);
  RUN_CASE(check_send_before_connect_reports_kClosed_never_throws);
  RUN_CASE(check_failing_handshake_reports_kClosed_never_throws);
  RUN_CASE(check_empty_url_connect_reports_kClosed_never_throws);
  RUN_CASE(check_migrate_before_connect_reports_kClosed_never_throws);
  RUN_CASE(check_double_migrate_ends_connected_never_throws);
  RUN_CASE(check_large_and_empty_datagrams_round_trip);
  RUN_CASE(check_send_after_close_reports_kClosed_never_throws);
  if (failures > 0) {
    std::printf("webtransport_test: %d failing\n", failures);
    return 1;
  }
  std::printf("webtransport_test: all passing\n");
  return 0;
}

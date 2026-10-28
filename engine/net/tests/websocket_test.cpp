// Batch 1 contract tests — T2 (FR-NET-005 / AC-NET-008 WS half;
// FR-NET-009 / AC-NET-017 WS deflate half).
//
// Hermetic: C++17 + engine/net headers only. The handshake transport is an
// in-process fake defined below; no sockets, no TLS, no env/config reads,
// no filesystem, no live services, no wall-clock sleeps.
//
// Binds to the T2 `Interface:` line in plan.md (WsState / WebSocket /
// WebSocketConfig / Listener; errors via onState(kClosed, reason), never
// exceptions; compressed and uncompressed paths deliver identical bytes).
// Type-shape ASSUMPTIONS (reconciliation rule: update call sites, not
// expectations, if the coder names these differently):
//   A-HS:   `WebSocketConfig::httpHandshake` is the injected `ITransport`
//           the frame/deflate logic runs over (plan Detail: "logic over
//           injected handshake transport"). The fake scripts the whole
//           session through canned handshake responses.
//   A-ECHO: the scripted handshake transport also carries the echo rule:
//           each canned post-handshake response is delivered to the
//           listener as one inbound message in script order. If the coder
//           delivers callbacks on a worker thread instead of synchronously,
//           the coder adds the wait/poll hook and these call sites gain a
//           wait — expected bytes/states do not change.
//   A-SYNC: listener callbacks fire before send()/close()/connect()
//           return (synchronous delivery). See A-ECHO for the async
//           reconciliation.
//   A-SUB:  the offer is asserted on the handshake request the client
//           sends over `httpHandshake` (RFC 6455 Sec-WebSocket-Protocol,
//           observable through the committed ITransport seam — no invented
//           API). The T2 Interface line exposes no selected-value
//           accessor, so the negotiated value itself is NOT asserted here;
//           see contract T2.md Conflicts (coder-added accessor reconciles
//           by adding one case, changing none).
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
#include "net/websocket.h"

namespace {

using muse::net::Error;
using muse::net::ExecCtx;
using muse::net::HttpClient;
using muse::net::ITransport;
using muse::net::Request;
using muse::net::Response;
using muse::net::StatusCode;
using muse::net::WebSocket;
using muse::net::WebSocketConfig;
using muse::net::WsState;

Response make_ok(const std::string& body, int status = 101) {
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

// Scripted handshake transport: records the handshake request, then replays
// canned responses in order. The i-th post-handshake canned body is the
// echo payload the client must deliver to onMessage (A-ECHO).
class FakeHandshake : public ITransport {
 public:
  explicit FakeHandshake(std::vector<Response> script)
      : script_(std::move(script)) {}

  Response execute(const Request& r, const ExecCtx&) override {
    seen_.push_back(r);
    if (failing_) return make_transport_error();
    if (at_ < script_.size()) return script_[at_++];
    return make_transport_error();
  }
  std::string name() const override { return "fake-handshake"; }

  void set_failing(bool f) { failing_ = f; }
  std::size_t calls() const { return seen_.size(); }
  const Request& call_at(std::size_t i) const { return seen_.at(i); }

 private:
  std::vector<Response> script_;
  std::vector<Request> seen_;
  std::size_t at_ = 0;
  bool failing_ = false;
};

struct Captured {
  bool opened = false;
  std::vector<std::pair<std::string, bool>> messages;  // (bytes, binary)
  std::vector<std::pair<WsState, std::string>> states;
};

typename WebSocket::Listener capture_into(Captured* c) {
  typename WebSocket::Listener l;
  l.onOpen = [c]() { c->opened = true; };
  l.onMessage = [c](std::string bytes, bool binary) {
    c->messages.emplace_back(std::move(bytes), binary);
  };
  l.onState = [c](WsState s, std::string reason) {
    c->states.emplace_back(s, std::move(reason));
  };
  return l;
}

bool last_state_is(const Captured& c, WsState s) {
  return !c.states.empty() && c.states.back().first == s;
}

// Script: handshake accept, then echo "hello" (text) and 3 binary bytes.
std::vector<Response> echo_script() {
  return {make_ok("accept"), make_ok("hello"), make_ok(std::string("\x01\x02\x03", 3))};
}

bool check_connect_emits_open() {
  auto hs = std::make_shared<FakeHandshake>(echo_script());
  WebSocketConfig cfg;
  cfg.permessageDeflate = false;
  cfg.httpHandshake = hs;
  WebSocket ws(cfg);
  Captured c;
  ws.setListener(capture_into(&c));
  try {
    ws.connect("wss://echo.test.invalid/chat");
  } catch (...) {
    return false;
  }
  return c.opened && hs->calls() >= 1;
}

bool check_handshake_carries_offered_subprotocols() {
  // A-SUB: the offer must reach the handshake request.
  auto hs = std::make_shared<FakeHandshake>(echo_script());
  WebSocketConfig cfg;
  cfg.permessageDeflate = false;
  cfg.subprotocols = {"graphql-ws", "json"};
  cfg.httpHandshake = hs;
  WebSocket ws(cfg);
  Captured c;
  ws.setListener(capture_into(&c));
  try {
    ws.connect("wss://echo.test.invalid/graphql");
  } catch (...) {
    return false;
  }
  if (hs->calls() < 1) return false;
  const Request& h = hs->call_at(0);
  bool offered = false;
  for (const auto& hdr : h.headers) {
    if (hdr.first == "Sec-WebSocket-Protocol") {
      if (hdr.second.find("graphql-ws") != std::string::npos &&
          hdr.second.find("json") != std::string::npos)
        offered = true;
    }
  }
  return offered;
}

bool check_ordered_text_binary_echo_deflate_on() {
  auto hs = std::make_shared<FakeHandshake>(echo_script());
  WebSocketConfig cfg;
  cfg.permessageDeflate = true;
  cfg.httpHandshake = hs;
  WebSocket ws(cfg);
  Captured c;
  ws.setListener(capture_into(&c));
  try {
    ws.connect("wss://echo.test.invalid/chat");
    ws.send("hello", false);
    ws.send(std::string("\x01\x02\x03", 3), true);
  } catch (...) {
    return false;
  }
  if (c.messages.size() != 2) return false;
  if (c.messages[0].first != "hello" || c.messages[0].second) return false;
  return c.messages[1].first == std::string("\x01\x02\x03", 3) &&
         c.messages[1].second;
}

bool check_deflate_off_matches_deflate_on_bytes() {
  // Plan T2 acceptance: compressed and uncompressed paths deliver
  // identical bytes.
  auto run = [](bool deflate) {
    auto hs = std::make_shared<FakeHandshake>(echo_script());
    WebSocketConfig cfg;
    cfg.permessageDeflate = deflate;
    cfg.httpHandshake = hs;
    WebSocket ws(cfg);
    Captured c;
    ws.setListener(capture_into(&c));
    ws.connect("wss://echo.test.invalid/chat");
    ws.send("hello", false);
    ws.send(std::string("\x01\x02\x03", 3), true);
    return c.messages;
  };
  std::vector<std::pair<std::string, bool>> on;
  std::vector<std::pair<std::string, bool>> off;
  try {
    on = run(true);
    off = run(false);
  } catch (...) {
    return false;
  }
  return on == off && on.size() == 2;
}

bool check_close_emits_kClosed_with_reason() {
  auto hs = std::make_shared<FakeHandshake>(echo_script());
  WebSocketConfig cfg;
  cfg.httpHandshake = hs;
  WebSocket ws(cfg);
  Captured c;
  ws.setListener(capture_into(&c));
  try {
    ws.connect("wss://echo.test.invalid/chat");
    ws.close();
  } catch (...) {
    return false;
  }
  if (!last_state_is(c, WsState::kClosed)) return false;
  return !c.states.back().second.empty();  // a reason must be reported
}

bool check_send_before_connect_reports_kClosed_never_throws() {
  auto hs = std::make_shared<FakeHandshake>(echo_script());
  WebSocketConfig cfg;
  cfg.httpHandshake = hs;
  WebSocket ws(cfg);
  Captured c;
  ws.setListener(capture_into(&c));
  try {
    ws.send("early", false);  // no connect(): must not throw
  } catch (...) {
    return false;
  }
  return last_state_is(c, WsState::kClosed) &&
         !c.states.back().second.empty();
}

bool check_failing_handshake_reports_kClosed_never_throws() {
  auto hs = std::make_shared<FakeHandshake>(echo_script());
  hs->set_failing(true);
  WebSocketConfig cfg;
  cfg.httpHandshake = hs;
  WebSocket ws(cfg);
  Captured c;
  ws.setListener(capture_into(&c));
  try {
    ws.connect("wss://echo.test.invalid/chat");
  } catch (...) {
    return false;
  }
  return !c.opened && last_state_is(c, WsState::kClosed) &&
         !c.states.back().second.empty();
}

bool check_empty_url_connect_reports_kClosed_never_throws() {
  auto hs = std::make_shared<FakeHandshake>(echo_script());
  WebSocketConfig cfg;
  cfg.httpHandshake = hs;
  WebSocket ws(cfg);
  Captured c;
  ws.setListener(capture_into(&c));
  try {
    ws.connect("");
  } catch (...) {
    return false;
  }
  return last_state_is(c, WsState::kClosed);
}

bool check_large_message_round_trips() {
  std::string big(1 << 20, 'w');  // 1 MiB boundary payload
  std::vector<Response> script = {make_ok("accept"), make_ok(big)};
  auto hs = std::make_shared<FakeHandshake>(std::move(script));
  WebSocketConfig cfg;
  cfg.permessageDeflate = true;
  cfg.httpHandshake = hs;
  WebSocket ws(cfg);
  Captured c;
  ws.setListener(capture_into(&c));
  try {
    ws.connect("wss://echo.test.invalid/chat");
    ws.send(big, false);
  } catch (...) {
    return false;
  }
  if (c.messages.size() != 1) return false;
  return c.messages[0].first == big && !c.messages[0].second;
}

bool check_empty_message_round_trips() {
  std::vector<Response> script = {make_ok("accept"), make_ok("")};
  auto hs = std::make_shared<FakeHandshake>(std::move(script));
  WebSocketConfig cfg;
  cfg.httpHandshake = hs;
  WebSocket ws(cfg);
  Captured c;
  ws.setListener(capture_into(&c));
  try {
    ws.connect("wss://echo.test.invalid/chat");
    ws.send("", false);
  } catch (...) {
    return false;
  }
  if (c.messages.size() != 1) return false;
  return c.messages[0].first.empty() && !c.messages[0].second;
}

bool check_double_close_stays_kClosed_never_throws() {
  auto hs = std::make_shared<FakeHandshake>(echo_script());
  WebSocketConfig cfg;
  cfg.httpHandshake = hs;
  WebSocket ws(cfg);
  Captured c;
  ws.setListener(capture_into(&c));
  try {
    ws.connect("wss://echo.test.invalid/chat");
    ws.close();
    ws.close();
  } catch (...) {
    return false;
  }
  return last_state_is(c, WsState::kClosed);
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
  RUN_CASE(check_connect_emits_open);
  RUN_CASE(check_handshake_carries_offered_subprotocols);
  RUN_CASE(check_ordered_text_binary_echo_deflate_on);
  RUN_CASE(check_deflate_off_matches_deflate_on_bytes);
  RUN_CASE(check_close_emits_kClosed_with_reason);
  RUN_CASE(check_send_before_connect_reports_kClosed_never_throws);
  RUN_CASE(check_failing_handshake_reports_kClosed_never_throws);
  RUN_CASE(check_empty_url_connect_reports_kClosed_never_throws);
  RUN_CASE(check_large_message_round_trips);
  RUN_CASE(check_empty_message_round_trips);
  RUN_CASE(check_double_close_stays_kClosed_never_throws);
  if (failures > 0) {
    std::printf("websocket_test: %d failing\n", failures);
    return 1;
  }
  std::printf("websocket_test: all passing\n");
  return 0;
}

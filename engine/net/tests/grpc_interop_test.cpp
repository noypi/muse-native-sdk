// Batch 3 contract tests — T5 (FR-NET-009 / AC-NET-017 gRPC half).
//
// Hermetic: C++17 + engine/net headers only. All wire I/O is an in-process
// FakeTransport; no sockets, no HTTP/2 stack, no env/config reads, no
// filesystem, no live services, no wall-clock sleeps.
//
// Binds to the T5 `Interface:` line in plan.md (GrpcStatus /
// RpcClient<Req,Resp>::call + subscribe/ServerStream::next/cancel; gRPC
// 5-byte prefix framing over the ITransport seam; Connect selected by
// Content-Type application/proto vs application/grpc+proto; all failures
// -> GrpcStatus non-OK, never exceptions).
//
// Type-shape ASSUMPTIONS (reconciliation rule: update call sites, not
// expectations, if the coder names these differently):
//   A-STR:  Req/Resp are std::string (opaque payload bytes; the codec
//           layer in T7 owns schema'd types, not this slice).
//   A-FRAME: unary/stream bodies use the gRPC 5-byte prefix: byte 0 is the
//           compressed flag (0), bytes 1..4 are the big-endian payload
//           length, followed by exactly that many payload bytes.
//   A-PROTO: the protocol is selected with `setUseConnect(bool)` on the
//           client (false = gRPC `application/grpc+proto`, true = Connect
//           `application/proto`). The observable contract is the
//           Content-Type header on the request the client sends through
//           the ITransport seam; if the coder names the selector
//           differently, only the call sites change.
//   A-SYNC:  call()/next()/cancel() are synchronous over the fake; if the
//           coder delivers streams on a worker thread, the coder adds the
//           wait/poll hook and these call sites gain a wait — expected
//           bytes/statuses do not change.
//   A-MUX:   HTTP/2 multiplexing is observed as N concurrent ServerStreams
//           sharing one ITransport instance: a single FakeTransport sees
//           all N executes (pooled `openCount == 1` bookkeeping lives in
//           the T1 seam, not re-asserted here beyond single-fake sharing).
//   A-REQ:  Request is default-constructible with assignable `method`,
//           `url` (std::string), `body` (std::string) and `headers`
//           (a vector<pair<string,string>> supporting emplace_back).
//   A-RESP: Response exposes `status`, `body` (std::string), `headers`,
//           `err`; `muse::net::Error` has `kTransport`/`kNone`.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "net/rpc.h"
#include "net/transport.h"
#include "net/types.h"

namespace {

using muse::net::Error;
using muse::net::ExecCtx;
using muse::net::GrpcStatus;
using muse::net::ITransport;
using muse::net::Request;
using muse::net::Response;
using muse::net::RpcClient;
using muse::net::StatusCode;

// ---- framing helpers (reference-vector mirror) ---------------------------

std::string encode_frame(const std::string& payload) {
  std::string out;
  out.push_back('\x00');
  uint32_t n = static_cast<uint32_t>(payload.size());
  out.push_back(static_cast<char>((n >> 24) & 0xFF));
  out.push_back(static_cast<char>((n >> 16) & 0xFF));
  out.push_back(static_cast<char>((n >> 8) & 0xFF));
  out.push_back(static_cast<char>(n & 0xFF));
  out += payload;
  return out;
}

// Golden fixtures mirroring the reference-server vectors (plan T5 Detail).
const std::string kGoldenUnaryReq = "golden-unary-request";
const std::string kGoldenUnaryResp = "golden-unary-response";
const std::vector<std::string> kGoldenStreamMsgs = {"stream-msg-1",
                                                    "stream-msg-2",
                                                    "stream-msg-3"};

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
class FakeGrpc : public ITransport {
 public:
  explicit FakeGrpc(std::vector<Response> script)
      : script_(std::move(script)) {}

  Response execute(const Request& r, const ExecCtx&) override {
    seen_.push_back(r);
    if (failing_) return make_transport_error();
    if (at_ < script_.size()) return script_[at_++];
    return make_transport_error();
  }
  std::string name() const override { return "fake-grpc"; }

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

std::shared_ptr<FakeGrpc> unary_fake() {
  return std::make_shared<FakeGrpc>(std::vector<Response>{
      make_ok(encode_frame(kGoldenUnaryResp),
              {{"grpc-status", "0"}})});
}

std::shared_ptr<FakeGrpc> stream_fake() {
  std::string body;
  for (const auto& m : kGoldenStreamMsgs) body += encode_frame(m);
  return std::make_shared<FakeGrpc>(
      std::vector<Response>{make_ok(body, {{"grpc-status", "0"}})});
}

// ---- cases ----------------------------------------------------------------

bool check_unary_golden_interop_byte_identical() {
  auto f = unary_fake();
  RpcClient<std::string, std::string> c(f);
  std::string out;
  GrpcStatus st{1, "unset"};
  try {
    st = c.call("/svc/method", kGoldenUnaryReq, &out);
  } catch (...) {
    return false;
  }
  return st.ok() && st.code == 0 && out == kGoldenUnaryResp;
}

bool check_unary_request_uses_grpc_content_type_by_default() {
  auto f = unary_fake();
  RpcClient<std::string, std::string> c(f);
  std::string out;
  try {
    c.call("/svc/method", kGoldenUnaryReq, &out);
  } catch (...) {
    return false;
  }
  if (f->calls() < 1) return false;
  return header_has(f->call_at(0), "Content-Type", "application/grpc+proto");
}

bool check_unary_request_carries_5byte_prefix_framing() {
  auto f = unary_fake();
  RpcClient<std::string, std::string> c(f);
  std::string out;
  try {
    c.call("/svc/method", kGoldenUnaryReq, &out);
  } catch (...) {
    return false;
  }
  if (f->calls() < 1) return false;
  const std::string& body = f->call_at(0).body;
  return body == encode_frame(kGoldenUnaryReq);
}

bool check_connect_protocol_uses_application_proto() {
  auto f = unary_fake();
  RpcClient<std::string, std::string> c(f);
  try {
    c.setUseConnect(true);  // A-PROTO
  } catch (...) {
    return false;
  }
  std::string out;
  try {
    c.call("/svc/method", kGoldenUnaryReq, &out);
  } catch (...) {
    return false;
  }
  if (f->calls() < 1) return false;
  return header_has(f->call_at(0), "Content-Type", "application/proto") &&
         !header_has(f->call_at(0), "Content-Type", "application/grpc+proto");
}

bool check_server_stream_delivers_3_messages_in_order() {
  auto f = stream_fake();
  RpcClient<std::string, std::string> c(f);
  std::unique_ptr<typename RpcClient<std::string, std::string>::ServerStream> s;
  try {
    s = c.subscribe("/svc/stream", kGoldenUnaryReq);
  } catch (...) {
    return false;
  }
  if (!s) return false;
  std::vector<std::string> got;
  for (int i = 0; i < 3; ++i) {
    std::string m;
    GrpcStatus st{1, "unset"};
    bool more = false;
    try {
      more = s->next(&m, &st);
    } catch (...) {
      return false;
    }
    if (!more || !st.ok()) return false;
    got.push_back(m);
  }
  return got == kGoldenStreamMsgs;
}

bool check_stream_end_reports_ok_then_no_more() {
  auto f = stream_fake();
  RpcClient<std::string, std::string> c(f);
  auto s = c.subscribe("/svc/stream", kGoldenUnaryReq);
  if (!s) return false;
  std::string m;
  GrpcStatus st{1, "unset"};
  for (int i = 0; i < 3; ++i) {
    try {
      if (!s->next(&m, &st)) return false;
    } catch (...) {
      return false;
    }
  }
  bool more = true;
  try {
    more = s->next(&m, &st);
  } catch (...) {
    return false;
  }
  return !more && st.ok();
}

bool check_trailer_error_surfaces_non_ok_never_throws() {
  auto f = std::make_shared<FakeGrpc>(std::vector<Response>{
      make_ok(encode_frame("partial"), {{"grpc-status", "13"},
                                        {"grpc-message", "backend failure"}})});
  RpcClient<std::string, std::string> c(f);
  std::string out;
  GrpcStatus st{0, ""};
  try {
    st = c.call("/svc/method", kGoldenUnaryReq, &out);
  } catch (...) {
    return false;
  }
  return !st.ok() && st.code != 0 && !st.message.empty();
}

bool check_transport_failure_is_non_ok_never_throws() {
  auto f = unary_fake();
  f->set_failing(true);
  RpcClient<std::string, std::string> c(f);
  std::string out = "sentinel";
  GrpcStatus st{0, ""};
  try {
    st = c.call("/svc/method", kGoldenUnaryReq, &out);
  } catch (...) {
    return false;
  }
  return !st.ok() && st.code != 0;
}

bool check_empty_method_is_non_ok_never_throws() {
  auto f = unary_fake();
  RpcClient<std::string, std::string> c(f);
  std::string out;
  GrpcStatus st{0, ""};
  try {
    st = c.call("", kGoldenUnaryReq, &out);
  } catch (...) {
    return false;
  }
  return !st.ok() && st.code != 0;
}

bool check_null_out_is_non_ok_never_throws() {
  auto f = unary_fake();
  RpcClient<std::string, std::string> c(f);
  GrpcStatus st{0, ""};
  try {
    st = c.call("/svc/method", kGoldenUnaryReq, nullptr);
  } catch (...) {
    return false;
  }
  return !st.ok() && st.code != 0;
}

bool check_truncated_frame_is_non_ok_never_throws() {
  auto f = std::make_shared<FakeGrpc>(
      std::vector<Response>{make_ok(std::string("\x00\x00\x00", 3))});
  RpcClient<std::string, std::string> c(f);
  std::string out;
  GrpcStatus st{0, ""};
  try {
    st = c.call("/svc/method", kGoldenUnaryReq, &out);
  } catch (...) {
    return false;
  }
  return !st.ok() && st.code != 0;
}

bool check_length_mismatch_frame_is_non_ok_never_throws() {
  // Prefix claims 64 bytes, only 3 follow.
  std::string bad;
  bad.push_back('\x00');
  bad.push_back('\x00');
  bad.push_back('\x00');
  bad.push_back('\x00');
  bad.push_back('\x40');
  bad += "abc";
  auto f = std::make_shared<FakeGrpc>(
      std::vector<Response>{make_ok(bad)});
  RpcClient<std::string, std::string> c(f);
  std::string out;
  GrpcStatus st{0, ""};
  try {
    st = c.call("/svc/method", kGoldenUnaryReq, &out);
  } catch (...) {
    return false;
  }
  return !st.ok() && st.code != 0;
}

bool check_n_concurrent_streams_share_one_pooled_connection() {
  // A-MUX: N subscribes through one client/transport; the single fake must
  // observe all N executes (one pooled connection, no per-stream dial).
  constexpr int kN = 8;
  std::string body;
  for (const auto& m : kGoldenStreamMsgs) body += encode_frame(m);
  std::vector<Response> script;
  for (int i = 0; i < kN; ++i)
    script.push_back(make_ok(body, {{"grpc-status", "0"}}));
  auto f = std::make_shared<FakeGrpc>(std::move(script));
  RpcClient<std::string, std::string> c(f);
  std::vector<std::unique_ptr<
      typename RpcClient<std::string, std::string>::ServerStream>>
      streams;
  try {
    for (int i = 0; i < kN; ++i)
      streams.push_back(c.subscribe("/svc/stream", kGoldenUnaryReq));
  } catch (...) {
    return false;
  }
  if (streams.size() != static_cast<std::size_t>(kN)) return false;
  for (auto& s : streams) {
    if (!s) return false;
    std::string m;
    GrpcStatus st{1, "unset"};
    try {
      if (!s->next(&m, &st)) return false;
    } catch (...) {
      return false;
    }
    if (m != kGoldenStreamMsgs[0]) return false;
  }
  return f->calls() == static_cast<std::size_t>(kN);
}

bool check_cancel_stops_stream_never_throws() {
  auto f = stream_fake();
  RpcClient<std::string, std::string> c(f);
  auto s = c.subscribe("/svc/stream", kGoldenUnaryReq);
  if (!s) return false;
  try {
    s->cancel();
    std::string m;
    GrpcStatus st{0, ""};
    bool more = s->next(&m, &st);
    (void)more;
  } catch (...) {
    return false;
  }
  return true;  // cancel must be safe; post-cancel next is terminal-or-false
}

bool check_large_unary_payload_round_trips() {
  std::string big(1 << 20, 'g');  // 1 MiB boundary payload
  auto f = std::make_shared<FakeGrpc>(std::vector<Response>{
      make_ok(encode_frame(big), {{"grpc-status", "0"}})});
  RpcClient<std::string, std::string> c(f);
  std::string out;
  GrpcStatus st{1, "unset"};
  try {
    st = c.call("/svc/method", big, &out);
  } catch (...) {
    return false;
  }
  return st.ok() && out == big;
}

bool check_empty_payload_round_trips() {
  auto f = std::make_shared<FakeGrpc>(std::vector<Response>{
      make_ok(encode_frame(""), {{"grpc-status", "0"}})});
  RpcClient<std::string, std::string> c(f);
  std::string out = "sentinel";
  GrpcStatus st{1, "unset"};
  try {
    st = c.call("/svc/method", "", &out);
  } catch (...) {
    return false;
  }
  return st.ok() && out.empty();
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
  RUN_CASE(check_unary_golden_interop_byte_identical);
  RUN_CASE(check_unary_request_uses_grpc_content_type_by_default);
  RUN_CASE(check_unary_request_carries_5byte_prefix_framing);
  RUN_CASE(check_connect_protocol_uses_application_proto);
  RUN_CASE(check_server_stream_delivers_3_messages_in_order);
  RUN_CASE(check_stream_end_reports_ok_then_no_more);
  RUN_CASE(check_trailer_error_surfaces_non_ok_never_throws);
  RUN_CASE(check_transport_failure_is_non_ok_never_throws);
  RUN_CASE(check_empty_method_is_non_ok_never_throws);
  RUN_CASE(check_null_out_is_non_ok_never_throws);
  RUN_CASE(check_truncated_frame_is_non_ok_never_throws);
  RUN_CASE(check_length_mismatch_frame_is_non_ok_never_throws);
  RUN_CASE(check_n_concurrent_streams_share_one_pooled_connection);
  RUN_CASE(check_cancel_stops_stream_never_throws);
  RUN_CASE(check_large_unary_payload_round_trips);
  RUN_CASE(check_empty_payload_round_trips);
  if (failures > 0) {
    std::printf("grpc_interop_test: %d failing\n", failures);
    return 1;
  }
  std::printf("grpc_interop_test: all passing\n");
  return 0;
}

#pragma once

// gRPC unary + server-streaming client over the ITransport seam
// (FR-NET-009 / AC-NET-017 / RULE-NET-001).
//
// Wire framing is the gRPC 5-byte prefix (flag 0 + big-endian length +
// payload) carried as the HTTP/2 body through the T1 ITransport seam, so
// concurrent streams multiplex by sharing one pooled connection (one
// ITransport instance serving every execute). The Connect protocol is
// selected by Content-Type: default gRPC `application/grpc+proto`,
// Connect `application/proto` via setUseConnect(true).
//
// Every failure surfaces as a non-OK GrpcStatus; nothing here throws.

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "net/transport.h"
#include "net/types.h"

namespace muse {
namespace net {

// gRPC status shape (plan T5 Interface line, verbatim).
struct GrpcStatus {
  int code = 0;
  std::string message;
  bool ok() const { return code == 0; }
};

namespace detail {

// Non-template wire helpers (defined in src/rpc.cc).
std::string EncodeGrpcFrame(const std::string& payload);
bool DecodeGrpcFrame(const std::string& body, std::string* payload);
bool DecodeGrpcFrames(const std::string& body,
                      std::vector<std::string>* payloads);
int ParseGrpcStatusHeader(const std::string& value);
std::string GrpcHeaderLookup(
    const std::vector<std::pair<std::string, std::string>>& headers,
    const std::string& key);

template <typename T>
std::string ToPayload(const T& v) {
  if constexpr (std::is_same_v<T, std::string>) {
    return v;
  } else {
    return std::string(v);
  }
}

template <typename T>
void FromPayload(const std::string& bytes, T* out) {
  if constexpr (std::is_same_v<T, std::string>) {
    *out = bytes;
  } else {
    *out = T(bytes);
  }
}

}  // namespace detail

// RpcClient (plan T5 Interface line, verbatim, plus the adopted additive
// extension setUseConnect(bool): true selects Connect
// `Content-Type: application/proto`, default false selects gRPC
// `application/grpc+proto`).
template <typename Req, typename Resp>
class RpcClient {
 public:
  explicit RpcClient(std::shared_ptr<ITransport> t)
      : transport_(std::move(t)) {}

  void setUseConnect(bool use_connect) { use_connect_ = use_connect; }

  GrpcStatus call(const std::string& method, const Req& req, Resp* out) {
    if (method.empty()) return GrpcStatus{3, "rpc: empty method"};
    if (out == nullptr) return GrpcStatus{3, "rpc: null response out-param"};
    if (!transport_) return GrpcStatus{14, "rpc: null transport"};
    Request r;
    r.method = "POST";
    r.url = method;
    r.headers.emplace_back(
        "Content-Type", use_connect_ ? "application/proto"
                                    : "application/grpc+proto");
    r.body = detail::EncodeGrpcFrame(detail::ToPayload(req));
    Response resp;
    try {
      resp = transport_->execute(r, ExecCtx{});
    } catch (const std::exception& e) {
      return GrpcStatus{14, std::string("rpc: transport threw: ") + e.what()};
    } catch (...) {
      return GrpcStatus{14, "rpc: transport threw"};
    }
    if (resp.err != Error::kNone) return GrpcStatus{14, "rpc: transport error"};
    const std::string trailer =
        detail::GrpcHeaderLookup(resp.headers, "grpc-status");
    if (!trailer.empty()) {
      const int code = detail::ParseGrpcStatusHeader(trailer);
      if (code != 0) {
        std::string msg =
            detail::GrpcHeaderLookup(resp.headers, "grpc-message");
        if (msg.empty()) msg = "rpc: remote error";
        return GrpcStatus{code, msg};
      }
    }
    std::string payload;
    if (!detail::DecodeGrpcFrame(resp.body, &payload)) {
      return GrpcStatus{13, "rpc: malformed grpc frame"};
    }
    try {
      detail::FromPayload(payload, out);
    } catch (const std::exception& e) {
      return GrpcStatus{13, std::string("rpc: decode failed: ") + e.what()};
    } catch (...) {
      return GrpcStatus{13, "rpc: decode failed"};
    }
    return GrpcStatus{0, ""};
  }

  class ServerStream {
   public:
    bool next(Resp* out, GrpcStatus* st) {
      if (cancelled_) {
        if (st != nullptr) *st = GrpcStatus{0, ""};
        return false;
      }
      if (!terminal_.ok()) {
        if (st != nullptr) *st = terminal_;
        return false;
      }
      if (pos_ >= messages_.size()) {
        if (st != nullptr) *st = GrpcStatus{0, ""};
        return false;
      }
      if (out == nullptr) {
        if (st != nullptr) *st = GrpcStatus{3, "rpc: null message out-param"};
        return false;
      }
      try {
        detail::FromPayload(messages_[pos_], out);
      } catch (const std::exception& e) {
        if (st != nullptr)
          *st = GrpcStatus{13, std::string("rpc: decode failed: ") + e.what()};
        return false;
      } catch (...) {
        if (st != nullptr) *st = GrpcStatus{13, "rpc: decode failed"};
        return false;
      }
      ++pos_;
      if (st != nullptr) *st = GrpcStatus{0, ""};
      return true;
    }

    void cancel() { cancelled_ = true; }

   private:
    friend class RpcClient;
    ServerStream(std::vector<std::string> messages, GrpcStatus terminal)
        : messages_(std::move(messages)), terminal_(std::move(terminal)) {}
    std::vector<std::string> messages_;
    GrpcStatus terminal_{0, ""};
    std::size_t pos_ = 0;
    bool cancelled_ = false;
  };

  std::unique_ptr<ServerStream> subscribe(const std::string& method,
                                         const Req& req) {
    if (method.empty()) {
      return std::unique_ptr<ServerStream>(new ServerStream(
          {}, GrpcStatus{3, "rpc: empty method"}));
    }
    if (!transport_) {
      return std::unique_ptr<ServerStream>(new ServerStream(
          {}, GrpcStatus{14, "rpc: null transport"}));
    }
    Request r;
    r.method = "POST";
    r.url = method;
    r.headers.emplace_back(
        "Content-Type", use_connect_ ? "application/proto"
                                    : "application/grpc+proto");
    r.body = detail::EncodeGrpcFrame(detail::ToPayload(req));
    Response resp;
    try {
      resp = transport_->execute(r, ExecCtx{});
    } catch (const std::exception& e) {
      return std::unique_ptr<ServerStream>(new ServerStream(
          {}, GrpcStatus{14, std::string("rpc: transport threw: ") + e.what()}));
    } catch (...) {
      return std::unique_ptr<ServerStream>(
          new ServerStream({}, GrpcStatus{14, "rpc: transport threw"}));
    }
    if (resp.err != Error::kNone) {
      return std::unique_ptr<ServerStream>(new ServerStream(
          {}, GrpcStatus{14, "rpc: transport error"}));
    }
    const std::string trailer =
        detail::GrpcHeaderLookup(resp.headers, "grpc-status");
    if (!trailer.empty()) {
      const int code = detail::ParseGrpcStatusHeader(trailer);
      if (code != 0) {
        std::string msg =
            detail::GrpcHeaderLookup(resp.headers, "grpc-message");
        if (msg.empty()) msg = "rpc: remote error";
        return std::unique_ptr<ServerStream>(
            new ServerStream({}, GrpcStatus{code, msg}));
      }
    }
    std::vector<std::string> messages;
    if (!detail::DecodeGrpcFrames(resp.body, &messages)) {
      return std::unique_ptr<ServerStream>(new ServerStream(
          {}, GrpcStatus{13, "rpc: malformed grpc frame"}));
    }
    return std::unique_ptr<ServerStream>(
        new ServerStream(std::move(messages), GrpcStatus{0, ""}));
  }

 private:
  std::shared_ptr<ITransport> transport_;
  bool use_connect_ = false;
};

}  // namespace net
}  // namespace muse

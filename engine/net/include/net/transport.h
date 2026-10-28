#pragma once

// Switchable-transport seam plus the transport-agnostic HTTP client
// (FR-NET-001 / AC-NET-002 / TC-NET-002).
//
// HttpClient carries the exact T1 interface line and additionally declares
// the T2 client-core surface (AddInterceptor / requestStream, FR-NET-004):
// exactly one HttpClient may exist in muse::net across the headers the
// shared contract test includes, so T2's net/http_client.h must re-export
// this class rather than redefine it. T2 method definitions belong in
// T2's src/http_client.cc; T1 methods are defined in src/transport.cc.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "net/types.h"

namespace muse {
namespace net {

// Switchable-transport seam (plan T1 Interface line, verbatim).
class ITransport {
 public:
  virtual ~ITransport() = default;
  virtual Response execute(const Request& r, const ExecCtx& ctx) = 0;
  virtual std::string name() const = 0;
};

// Transport-agnostic HTTP client (plan T1 Interface line, verbatim).
//
// request() delegates to the bound transport and reports every failure as
// err == kTransport — including empty URLs, a null transport, and a
// transport that throws — so no input ever escapes as an exception.
// An HTTP error status delivered by the transport is data (err == kNone).
class HttpClient {
 public:
  explicit HttpClient(std::shared_ptr<ITransport> t, ClientConfig cfg);
  Response request(const Request& r);
  void setTransport(std::shared_ptr<ITransport> t);

  // T2 client-core surface (FR-NET-004): interceptors run in registration
  // order around transport execute; requestStream delivers the body as
  // ordered chunks whose concatenation equals the returned body.
  void AddInterceptor(Interceptor i);
  Response requestStream(const Request& r,
                         std::function<void(BodyChunk)> onChunk);

 private:
  std::shared_ptr<ITransport> transport_;
  ClientConfig config_;
  std::vector<Interceptor> interceptors_;
};

}  // namespace net
}  // namespace muse

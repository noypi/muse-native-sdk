#pragma once

#include <string>

#include "net/transport.h"

namespace muse::net {

// Cronet transport configuration. QUIC internals are faked via injection in
// tests; `injectFailure` forces execute() onto the transport-failure path
// in-process (no sockets, no I/O).
struct CronetConfig {
  bool injectFailure = false;
};

// Cronet transport plug behind main's ITransport seam (FR-NET-004 surface):
// executes against main's Request/Response/ExecCtx vocabulary so HttpClient
// (interceptors, requestStream) composes over it unchanged. Transport failures
// return Response{status=kUnknown, err=Error::kTransport} and never throw.
// Interceptor chaining lives on HttpClient, not on the transport.
class CronetTransport : public ITransport {
 public:
  explicit CronetTransport(CronetConfig cfg);
  Response execute(const Request&, const ExecCtx&) override;
  std::string name() const override;

 private:
  CronetConfig cfg_;
};

}  // namespace muse::net

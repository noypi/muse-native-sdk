#pragma once

// T6 default-transport config (FR-NET-014 / AC-NET-027).
// Cronet is the default transport; BoringSSL remains selectable per
// build/init choice with no application code change (AC-NET-002 preserved).
// Header-only: no new TU so no CMake change is required.

#include <memory>
#include <stdexcept>
#include <string>

#include "net/cronet_transport.h"
#include "net/transport.h"

namespace muse {
namespace net {

// BoringSSL fallback transport behind the ITransport seam. In-process stub
// with the same success semantics as CronetTransport (injectFailure=false):
// a valid http(s) URL answers err == kNone / status kOk / body "ok";
// anything else fails closed with err == kTransport and never throws.
class BoringsslTransport : public ITransport {
 public:
  Response execute(const Request& req, const ExecCtx& /*ctx*/) override {
    try {
      constexpr std::size_t kMaxUrlLength = 8192;
      if (req.url.empty() || req.url.size() > kMaxUrlLength) {
        return TransportFailure();
      }
      if (req.url.compare(0, 8, "https://") != 0 &&
          req.url.compare(0, 7, "http://") != 0) {
        return TransportFailure();
      }
      Response r;
      r.status = StatusCode::kOk;
      r.body = "ok";
      r.err = Error::kNone;
      return r;
    } catch (...) {
      return TransportFailure();
    }
  }

  std::string name() const override { return "boringssl"; }

 private:
  static Response TransportFailure() {
    Response r;
    r.status = static_cast<StatusCode>(0);
    r.body.clear();
    r.err = Error::kTransport;
    return r;
  }
};

// Default transport name: cronet.
inline std::string DefaultTransportName() { return "cronet"; }

// Binds either the Cronet or the BoringSSL transport onto the client with
// the identical request/response API. Unknown, empty, or hostile names throw
// (std::invalid_argument) and leave the prior binding untouched.
inline void SelectTransport(const std::string& name, HttpClient& client) {
  if (name == "cronet") {
    client.setTransport(
        std::make_shared<CronetTransport>(CronetConfig{}));
    return;
  }
  if (name == "boringssl") {
    client.setTransport(std::make_shared<BoringsslTransport>());
    return;
  }
  throw std::invalid_argument("muse::net: unknown transport: " + name);
}

}  // namespace net
}  // namespace muse

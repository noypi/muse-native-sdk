#include "net/cronet_transport.h"

#include <string>

namespace muse::net {
namespace {

// Fail-closed URL gate (branch T1 behavior, now file-local since main's
// transport.h owns no URL helper): empty, oversized, or non-http(s) URLs are
// rejected without throwing and without touching any socket.
bool IsValidCronetUrl(const std::string& url) {
  constexpr std::size_t kMaxUrlLength = 8192;
  if (url.empty() || url.size() > kMaxUrlLength) {
    return false;
  }
  return url.compare(0, 8, "https://") == 0 ||
         url.compare(0, 7, "http://") == 0;
}

// Failure to execute: empty body, zero status, kTransport. Never throws.
Response TransportFailure() {
  Response r;
  r.status = static_cast<StatusCode>(0);
  r.body.clear();
  r.err = Error::kTransport;
  return r;
}

}  // namespace

CronetTransport::CronetTransport(CronetConfig cfg) : cfg_(cfg) {}

Response CronetTransport::execute(const Request& req, const ExecCtx& /*ctx*/) {
  try {
    if (cfg_.injectFailure) {
      return TransportFailure();
    }
    if (!IsValidCronetUrl(req.url)) {
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

std::string CronetTransport::name() const { return "cronet"; }

}  // namespace muse::net

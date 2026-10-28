#include "net/transport_policy.h"

#include <cstdint>
#include <exception>
#include <string>
#include <utility>

namespace muse::net {
namespace {

// Fail-closed URL gate (branch T1 behavior, file-local): empty, oversized,
// or non-http(s) URLs are rejected without throwing and without socket use.
bool IsValidPolicyUrl(const std::string& url) {
  constexpr std::size_t kMaxUrlLength = 8192;
  if (url.empty() || url.size() > kMaxUrlLength) {
    return false;
  }
  return url.compare(0, 8, "https://") == 0 ||
         url.compare(0, 7, "http://") == 0;
}

// Fail-closed origin parse: only http(s) URLs with a non-empty host yield an
// origin; anything else has no origin and the request fails closed. Keys on
// main's Origin shape; the policy ignores the port.
bool ParseOrigin(const std::string& url, Origin* out) {
  std::string scheme;
  std::string rest;
  if (url.compare(0, 8, "https://") == 0) {
    scheme = "https";
    rest = url.substr(8);
  } else if (url.compare(0, 7, "http://") == 0) {
    scheme = "http";
    rest = url.substr(7);
  } else {
    return false;
  }
  const std::size_t end = rest.find_first_of("/?#");
  const std::string authority =
      (end == std::string::npos) ? rest : rest.substr(0, end);
  // Strip optional userinfo and port; the policy keys on the host alone.
  std::string host = authority;
  const std::size_t at = host.find_last_of('@');
  if (at != std::string::npos) {
    host = host.substr(at + 1);
  }
  const std::size_t colon = host.find_last_of(':');
  if (colon != std::string::npos) {
    host = host.substr(0, colon);
  }
  if (host.empty()) {
    return false;
  }
  out->scheme = scheme;
  out->host = host;
  out->port = (scheme == "https") ? 443 : 80;
  return true;
}

// Denial and failure share main's failure-to-execute shape: zero status,
// err == kTransport. Main's Error vocabulary has no kPolicyDenied, so a
// denied request is indistinguishable from a transport failure by design.
Response PolicyDenied() {
  Response r;
  r.status = static_cast<StatusCode>(0);
  r.body.clear();
  r.err = Error::kTransport;
  return r;
}

Response TransportFailure() {
  Response r;
  r.status = static_cast<StatusCode>(0);
  r.body.clear();
  r.err = Error::kTransport;
  return r;
}

// In-process default inner transport (branch T1 oracle behavior, file-local):
// same fail-closed URL gate as CronetTransport, "ok"/kNone on success, never
// throws. Interceptor chaining lives on HttpClient, not here.
class DefaultTransport : public ITransport {
 public:
  DefaultTransport() = default;

  Response execute(const Request& req, const ExecCtx& /*ctx*/) override {
    try {
      if (!IsValidPolicyUrl(req.url)) {
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

  std::string name() const override { return "default"; }
};

// Policy gate shared by both transports: consults the same injected
// ITlsPolicy before delegating. Denial happens before any socket use — the
// inner transport (and factory) is never touched on deny.
class PolicyEnforcingTransport : public ITransport {
 public:
  PolicyEnforcingTransport(std::shared_ptr<ITransport> inner, ITlsPolicy& tls)
      : inner_(std::move(inner)), tls_(tls) {}

  Response execute(const Request& req, const ExecCtx& ctx) override {
    try {
      if (!IsValidPolicyUrl(req.url)) {
        return TransportFailure();
      }
      Origin origin;
      if (!ParseOrigin(req.url, &origin)) {
        return TransportFailure();
      }
      bool allowed = false;
      try {
        if (origin.scheme == "http") {
          allowed = tls_.isCleartextAllowed(origin);
        } else {
          allowed = tls_.isPinned(origin);
        }
      } catch (...) {
        return PolicyDenied();
      }
      if (!allowed) {
        return PolicyDenied();
      }
      return inner_->execute(req, ctx);
    } catch (...) {
      return TransportFailure();
    }
  }

  std::string name() const override { return inner_->name(); }

 private:
  std::shared_ptr<ITransport> inner_;
  ITlsPolicy& tls_;
};

}  // namespace

std::shared_ptr<ITransport> CreateTransport(const AppNetConfig& config,
                                            ITlsPolicy& tls,
                                            IQuicSocketFactory* /*factory*/) {
  std::shared_ptr<ITransport> inner;
  if (config.transport == TransportKind::kCronet) {
    inner = std::make_shared<CronetTransport>(CronetConfig{});
  } else {
    inner = std::make_shared<DefaultTransport>();
  }
  return std::make_shared<PolicyEnforcingTransport>(std::move(inner), tls);
}

}  // namespace muse::net

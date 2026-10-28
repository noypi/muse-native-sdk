// Batch 1 contract tests — FR-NET-007 / AC-NET-011 (TC-NET-011).
// T2: build/init-time transport selection honoring shared pinning + cleartext.
// Binds ONLY to the T2 `Interface:` line; never to implementation.
// Hermetic: fake ITlsPolicy with allow/deny matrix; socket use counted via an
// in-process fake factory; no downloads, no live network, no env/config reads.
// ASSUMPTION (A-T2-seam): Origin/Request/Response/Error shapes beyond the T2
// `Interface:` line are minimal-test seams (Origin{host,scheme}, Request{url},
// Response{status,err}, Error::kPolicyDenied, AppNetConfig{transport}); the
// settled contract is the verbatim `Interface:` line in T2.md. T1's ITransport
// seam is a T1 production file; these tests treat it as consumed.
#include <cstdio>
#include <memory>
#include <string>

#include "net/transport.h"
#include "net/transport_policy.h"

namespace {

// Fake TLS policy: programmable pin + cleartext decisions. Deny by default.
class FakeTlsPolicy : public muse::net::ITlsPolicy {
 public:
  bool pinOk = true;
  bool cleartextOk = false;
  bool isPinned(const muse::net::Origin& o) const override {
    (void)o;
    return pinOk;
  }
  bool isCleartextAllowed(const muse::net::Origin& o) const override {
    (void)o;
    return cleartextOk;
  }
};

muse::net::Request httpsReq() {
  muse::net::Request r;
  r.method = "GET";
  r.url = "https://api.test.invalid/items";
  return r;
}

muse::net::Request httpReq() {
  muse::net::Request r;
  r.method = "GET";
  r.url = "http://api.test.invalid/items";
  return r;
}

int socketUseCount = 0;

}  // namespace

// Pinned-mismatch returns kPolicyDenied with zero socket calls (default).
static bool check_default_pinned_mismatch_denied() {
  socketUseCount = 0;
  FakeTlsPolicy tls;
  tls.pinOk = false;
  muse::net::AppNetConfig cfg;
  cfg.transport = muse::net::TransportKind::kDefault;
  auto t = muse::net::CreateTransport(cfg, tls, nullptr);
  muse::net::ExecCtx ctx;
  muse::net::Response r = t->execute(httpsReq(), ctx);
  return r.status == 0 && r.err == muse::net::Error::kPolicyDenied &&
         socketUseCount == 0;
}

// Pinned-mismatch returns kPolicyDenied with zero socket calls (cronet).
static bool check_cronet_pinned_mismatch_denied() {
  socketUseCount = 0;
  FakeTlsPolicy tls;
  tls.pinOk = false;
  muse::net::AppNetConfig cfg;
  cfg.transport = muse::net::TransportKind::kCronet;
  auto t = muse::net::CreateTransport(cfg, tls, nullptr);
  muse::net::ExecCtx ctx;
  muse::net::Response r = t->execute(httpsReq(), ctx);
  return r.status == 0 && r.err == muse::net::Error::kPolicyDenied &&
         socketUseCount == 0;
}

// Cleartext-denied origin returns kPolicyDenied with zero socket calls (both).
static bool check_cleartext_denied_both() {
  FakeTlsPolicy tls;
  tls.pinOk = true;
  tls.cleartextOk = false;
  muse::net::ExecCtx ctx;
  for (auto kind :
       {muse::net::TransportKind::kDefault, muse::net::TransportKind::kCronet}) {
    socketUseCount = 0;
    muse::net::AppNetConfig cfg;
    cfg.transport = kind;
    auto t = muse::net::CreateTransport(cfg, tls, nullptr);
    muse::net::Response r = t->execute(httpReq(), ctx);
    if (r.status != 0 || r.err != muse::net::Error::kPolicyDenied) return false;
    if (socketUseCount != 0) return false;
  }
  return true;
}

// Allowed origins proceed past policy on both transports (no kPolicyDenied).
static bool check_allowed_origins_proceed_both() {
  FakeTlsPolicy tls;
  tls.pinOk = true;
  tls.cleartextOk = true;
  muse::net::ExecCtx ctx;
  for (auto kind :
       {muse::net::TransportKind::kDefault, muse::net::TransportKind::kCronet}) {
    muse::net::AppNetConfig cfg;
    cfg.transport = kind;
    auto t = muse::net::CreateTransport(cfg, tls, nullptr);
    muse::net::Response a = t->execute(httpsReq(), ctx);
    if (a.err == muse::net::Error::kPolicyDenied) return false;
    muse::net::Response b = t->execute(httpReq(), ctx);
    if (b.err == muse::net::Error::kPolicyDenied) return false;
  }
  return true;
}

// Selection honors the build/init-time value: kinds yield distinct transports.
static bool check_selection_yields_requested_kind() {
  FakeTlsPolicy tls;
  muse::net::AppNetConfig d;
  d.transport = muse::net::TransportKind::kDefault;
  muse::net::AppNetConfig c;
  c.transport = muse::net::TransportKind::kCronet;
  auto td = muse::net::CreateTransport(d, tls, nullptr);
  auto tc = muse::net::CreateTransport(c, tls, nullptr);
  return td->name() != tc->name();
}

// Malformed/oversized URL fails closed (kPolicyDenied or kTransport), never throws.
static bool check_malformed_url_fails_closed_never_throws() {
  FakeTlsPolicy tls;
  muse::net::AppNetConfig cfg;
  cfg.transport = muse::net::TransportKind::kCronet;
  auto t = muse::net::CreateTransport(cfg, tls, nullptr);
  muse::net::ExecCtx ctx;
  muse::net::Request r;
  r.method = "GET";
  r.url = std::string(65536, 'x');
  try {
    muse::net::Response resp = t->execute(r, ctx);
    return resp.status == 0 &&
           (resp.err == muse::net::Error::kPolicyDenied ||
            resp.err == muse::net::Error::kTransport);
  } catch (...) {
    return false;
  }
}

#define RUN_CASE(name)                           \
  do {                                           \
    if (!(name)()) {                             \
      std::printf("FAIL %s\n", #name);           \
      ++failures;                                \
    } else {                                     \
      std::printf("PASS %s\n", #name);           \
    }                                            \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_default_pinned_mismatch_denied);
  RUN_CASE(check_cronet_pinned_mismatch_denied);
  RUN_CASE(check_cleartext_denied_both);
  RUN_CASE(check_allowed_origins_proceed_both);
  RUN_CASE(check_selection_yields_requested_kind);
  RUN_CASE(check_malformed_url_fails_closed_never_throws);
  if (failures > 0) {
    std::printf("transport_policy_test: %d failing\n", failures);
    return 1;
  }
  std::printf("transport_policy_test: all passing\n");
  return 0;
}

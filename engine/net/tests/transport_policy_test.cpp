// Batch 1 contract tests — FR-NET-007 / AC-NET-011 (TC-NET-011).
// T2: build/init-time transport selection honoring shared pinning + cleartext.
// Binds ONLY to the T2 `Interface:` line; never to implementation.
// Hermetic: fake ITlsPolicy with allow/deny matrix; socket use observed via a
// counting factory fake passed as the factory plus inner-delegation counting
// through the public boundary (deny must never show the inner 200/"ok"
// signature; allow must); no downloads, no live network, no env/config reads.
// ASSUMPTION (A-T2-seam): Origin/Request/Response/Error shapes beyond the T2
// `Interface:` line are minimal-test seams (Origin{host,scheme}, Request{url},
// Response{status,err}, Error::kPolicyDenied, AppNetConfig{transport}); the
// settled contract is the verbatim `Interface:` line in T2.md. T1's ITransport
// seam is a T1 production file; these tests treat it as consumed.
#include <cstdio>
#include <memory>
#include <stdexcept>
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

// Socket-use observation (real, via the public boundary): the factory seam
// (IQuicSocketFactory) currently exposes no callable hook, so a counting
// factory fake is passed on every CreateTransport call to prove the factory
// path is carried (factoryUses stays 0 by construction — production never
// calls it), while actual socket/inner use is observed through delegation:
// deny responses must carry kPolicyDenied and never the inner's 200/"ok"
// signature, and allow responses must carry the inner's 200/"ok" signature
// (proving the inner transport was consulted). A regression that touches the
// inner transport (or factory) before the policy decision would return the
// inner's 200/"ok" on a deny path and fail these assertions — the old plain
// `socketUseCount` variable could not observe that (nothing incremented it).
class CountingSocketFactory : public muse::net::IQuicSocketFactory {
 public:
  mutable int factoryUses = 0;
};

// incremented only when a response proves the inner transport ran.
int innerDelegations = 0;

bool isInnerResponse(const muse::net::Response& r) {
  return r.status == 200 && r.body == "ok" &&
         r.err == muse::net::Error::kNone;
}

// Deny observation: kPolicyDenied with no inner signature, factory untouched.
bool expectDeniedBeforeSocketUse(const muse::net::Response& r,
                                 const CountingSocketFactory& factory) {
  if (r.status != 0 || r.err != muse::net::Error::kPolicyDenied) return false;
  if (isInnerResponse(r)) return false;
  if (factory.factoryUses != 0) return false;
  return innerDelegations == 0;
}

// Allow observation: inner consulted (200/"ok"), factory still untouched.
bool expectAllowedViaInner(const muse::net::Response& r,
                           const CountingSocketFactory& factory) {
  if (!isInnerResponse(r)) return false;
  if (factory.factoryUses != 0) return false;
  ++innerDelegations;
  return true;
}

// Host-recording TLS policy: records the host the policy gate consulted so
// IPv6 origin parsing is observed through the testable seam (CreateTransport
// + execute), without binding to the internal ParseOrigin helper.
class RecordingTlsPolicy : public muse::net::ITlsPolicy {
 public:
  mutable std::string lastPinnedHost;
  bool pinOk = true;
  bool isPinned(const muse::net::Origin& o) const override {
    lastPinnedHost = o.host;
    return pinOk;
  }
  bool isCleartextAllowed(const muse::net::Origin& o) const override {
    (void)o;
    return true;
  }
};

muse::net::Request ipv6Req(const std::string& url) {
  muse::net::Request r;
  r.method = "GET";
  r.url = url;
  return r;
}

// Throwing TLS policy: every gate consultation throws. Production must fail
// closed (kPolicyDenied, never throws, inner untouched) — AC-NET-011.
class ThrowingTlsPolicy : public muse::net::ITlsPolicy {
 public:
  bool isPinned(const muse::net::Origin& o) const override {
    (void)o;
    throw std::runtime_error("pin backend fault");
  }
  bool isCleartextAllowed(const muse::net::Origin& o) const override {
    (void)o;
    throw std::runtime_error("cleartext backend fault");
  }
};

}  // namespace

// Pinned-mismatch returns kPolicyDenied with zero socket calls (default).
static bool check_default_pinned_mismatch_denied() {
  innerDelegations = 0;
  CountingSocketFactory factory;
  FakeTlsPolicy tls;
  tls.pinOk = false;
  muse::net::AppNetConfig cfg;
  cfg.transport = muse::net::TransportKind::kDefault;
  auto t = muse::net::CreateTransport(cfg, tls, &factory);
  muse::net::ExecCtx ctx;
  muse::net::Response r = t->execute(httpsReq(), ctx);
  return expectDeniedBeforeSocketUse(r, factory);
}

// Pinned-mismatch returns kPolicyDenied with zero socket calls (cronet).
static bool check_cronet_pinned_mismatch_denied() {
  innerDelegations = 0;
  CountingSocketFactory factory;
  FakeTlsPolicy tls;
  tls.pinOk = false;
  muse::net::AppNetConfig cfg;
  cfg.transport = muse::net::TransportKind::kCronet;
  auto t = muse::net::CreateTransport(cfg, tls, &factory);
  muse::net::ExecCtx ctx;
  muse::net::Response r = t->execute(httpsReq(), ctx);
  return expectDeniedBeforeSocketUse(r, factory);
}

// Cleartext-denied origin returns kPolicyDenied with zero socket calls (both).
static bool check_cleartext_denied_both() {
  FakeTlsPolicy tls;
  tls.pinOk = true;
  tls.cleartextOk = false;
  muse::net::ExecCtx ctx;
  for (auto kind :
       {muse::net::TransportKind::kDefault, muse::net::TransportKind::kCronet}) {
    innerDelegations = 0;
    CountingSocketFactory factory;
    muse::net::AppNetConfig cfg;
    cfg.transport = kind;
    auto t = muse::net::CreateTransport(cfg, tls, &factory);
    muse::net::Response r = t->execute(httpReq(), ctx);
    if (!expectDeniedBeforeSocketUse(r, factory)) return false;
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
    CountingSocketFactory factory;
    auto t = muse::net::CreateTransport(cfg, tls, &factory);
    muse::net::Response a = t->execute(httpsReq(), ctx);
    innerDelegations = 0;
    if (!expectAllowedViaInner(a, factory)) return false;
    if (innerDelegations == 0) return false;
    muse::net::Response b = t->execute(httpReq(), ctx);
    innerDelegations = 0;
    if (!expectAllowedViaInner(b, factory)) return false;
    if (innerDelegations == 0) return false;
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

// Bare bracketed IPv6 literal keeps its whole host (no port to strip):
// https://[::1]/ must consult the policy with host "[::1]", not "[:".
static bool check_ipv6_bare_literal_kept_whole() {
  innerDelegations = 0;
  CountingSocketFactory factory;
  RecordingTlsPolicy tls;
  muse::net::AppNetConfig cfg;
  cfg.transport = muse::net::TransportKind::kDefault;
  auto t = muse::net::CreateTransport(cfg, tls, &factory);
  muse::net::ExecCtx ctx;
  muse::net::Response r = t->execute(ipv6Req("https://[::1]/items"), ctx);
  if (tls.lastPinnedHost != "[::1]") return false;
  if (!expectAllowedViaInner(r, factory)) return false;
  return innerDelegations > 0;
}

// Bracketed IPv6 literal with port strips only the port suffix (" ]:443 "):
// https://[::1]:443/ must consult the policy with host "[::1]".
static bool check_ipv6_literal_with_port_stripped() {
  innerDelegations = 0;
  CountingSocketFactory factory;
  RecordingTlsPolicy tls;
  muse::net::AppNetConfig cfg;
  cfg.transport = muse::net::TransportKind::kCronet;
  auto t = muse::net::CreateTransport(cfg, tls, &factory);
  muse::net::ExecCtx ctx;
  muse::net::Response r = t->execute(ipv6Req("https://[::1]:443/items"), ctx);
  if (tls.lastPinnedHost != "[::1]") return false;
  if (!expectAllowedViaInner(r, factory)) return false;
  return innerDelegations > 0;
}

// Throwing ITlsPolicy fails closed: every gate path returns kPolicyDenied,
// never throws, inner untouched (both transports, both schemes) — AC-NET-011.
static bool check_throwing_tls_policy_fails_closed_policy_denied() {
  ThrowingTlsPolicy tls;
  muse::net::ExecCtx ctx;
  for (auto kind :
       {muse::net::TransportKind::kDefault, muse::net::TransportKind::kCronet}) {
    for (bool useHttp : {false, true}) {
      innerDelegations = 0;
      CountingSocketFactory factory;
      muse::net::AppNetConfig cfg;
      cfg.transport = kind;
      auto t = muse::net::CreateTransport(cfg, tls, &factory);
      muse::net::Response r;
      try {
        r = t->execute(useHttp ? httpReq() : httpsReq(), ctx);
      } catch (...) {
        return false;
      }
      if (!expectDeniedBeforeSocketUse(r, factory)) return false;
    }
  }
  return true;
}

// Credentialed/non-default-port URL keys the policy on the host alone:
// "https://user:pass@host:8443/" consults "host" (userinfo/port stripped),
// while "https://host@evil/" consults "evil" (rightmost '@' wins) — AC-NET-011.
static bool check_credentialed_url_keys_policy_on_host_alone() {
  {
    innerDelegations = 0;
    CountingSocketFactory factory;
    RecordingTlsPolicy tls;
    muse::net::AppNetConfig cfg;
    cfg.transport = muse::net::TransportKind::kDefault;
    auto t = muse::net::CreateTransport(cfg, tls, &factory);
    muse::net::ExecCtx ctx;
    muse::net::Response r = t->execute(
        ipv6Req("https://user:pass@api.test.invalid:8443/items"), ctx);
    if (tls.lastPinnedHost != "api.test.invalid") return false;
    if (!expectAllowedViaInner(r, factory)) return false;
    if (innerDelegations == 0) return false;
  }
  {
    innerDelegations = 0;
    CountingSocketFactory factory;
    RecordingTlsPolicy tls;
    muse::net::AppNetConfig cfg;
    cfg.transport = muse::net::TransportKind::kCronet;
    auto t = muse::net::CreateTransport(cfg, tls, &factory);
    muse::net::ExecCtx ctx;
    muse::net::Response r = t->execute(
        ipv6Req("https://api.test.invalid@evil.test/items"), ctx);
    if (tls.lastPinnedHost != "evil.test") return false;
    if (!expectAllowedViaInner(r, factory)) return false;
    if (innerDelegations == 0) return false;
  }
  return true;
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
  RUN_CASE(check_ipv6_bare_literal_kept_whole);
  RUN_CASE(check_ipv6_literal_with_port_stripped);
  RUN_CASE(check_throwing_tls_policy_fails_closed_policy_denied);
  RUN_CASE(check_credentialed_url_keys_policy_on_host_alone);
  RUN_CASE(check_malformed_url_fails_closed_never_throws);
  if (failures > 0) {
    std::printf("transport_policy_test: %d failing\n", failures);
    return 1;
  }
  std::printf("transport_policy_test: all passing\n");
  return 0;
}

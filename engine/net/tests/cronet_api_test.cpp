// Batch 1 contract tests — FR-NET-007 / AC-NET-010 (TC-NET-010).
// T1: CronetTransport implements the same C++ request/response/interceptor API.
// Binds ONLY to the T1 `Interface:` line; never to implementation.
// Hermetic: fakes behind injected interfaces only; no downloads, no live network,
// no env/config reads. QUIC internals faked via injected IQuicSocket-shaped fake
// owned by the test; failures injected in-process.
// ASSUMPTION (A-T1-seam): the ITransport/Request/Response/ExecCtx/Error seam from
// run fr-net-004-fr-net-001-fr-net-008 is absent in this worktree (verified
// 2026-10-28: engine/net/include/net/ holds only data-layer headers); the coder
// re-creates the minimal seam inside the T1 production files. Field-level shapes
// below (Request{method,url,body}, Response{status,body,err}, Error::kTransport,
// CronetConfig with injected socket factory, AddInterceptor free function) are
// assumptions; the settled contract is the verbatim `Interface:` line in T1.md.
#include <cstdio>
#include <string>

#include "net/transport.h"
#include "net/cronet_transport.h"

namespace {

// In-process fake socket factory: programmable response or forced failure.
// No threads, no I/O, no env.
struct FakeQuicSocket {
  bool fail = false;
  int status = 200;
  std::string body = "ok";
};

muse::net::Request makeGet() {
  muse::net::Request r;
  r.method = "GET";
  r.url = "https://api.test.invalid/items";
  return r;
}

muse::net::Request makePost() {
  muse::net::Request r;
  r.method = "POST";
  r.url = "https://api.test.invalid/items";
  r.body = "{\"a\":1}";
  return r;
}

}  // namespace

// Same request sequence through FakeDefaultTransport and CronetTransport yields
// identical application-visible results (transport-agnostic API).
static bool check_same_api_get_post_identical() {
  muse::net::CronetConfig cfg;
  muse::net::CronetTransport cronet(cfg);
  muse::net::FakeDefaultTransport def;
  muse::net::ExecCtx ctx;
  muse::net::Response a = def.execute(makeGet(), ctx);
  muse::net::Response b = cronet.execute(makeGet(), ctx);
  if (a.status != b.status || a.body != b.body || a.err != b.err) return false;
  muse::net::Response c = def.execute(makePost(), ctx);
  muse::net::Response d = cronet.execute(makePost(), ctx);
  return c.status == d.status && c.body == d.body && c.err == d.err;
}

// Interceptor chain runs identically around either transport (order recorded).
static bool check_interceptor_order_identical() {
  std::string orderDef;
  std::string orderCronet;
  muse::net::CronetConfig cfg;
  muse::net::CronetTransport cronet(cfg);
  muse::net::FakeDefaultTransport def;
  muse::net::ExecCtx ctx;
  muse::net::AddInterceptor(def, [&](const muse::net::Request& r) { orderDef += "i1;"; });
  muse::net::AddInterceptor(def, [&](const muse::net::Request& r) { orderDef += "i2;"; });
  muse::net::AddInterceptor(cronet, [&](const muse::net::Request& r) { orderCronet += "i1;"; });
  muse::net::AddInterceptor(cronet, [&](const muse::net::Request& r) { orderCronet += "i2;"; });
  (void)def.execute(makeGet(), ctx);
  (void)cronet.execute(makeGet(), ctx);
  return !orderDef.empty() && orderDef == orderCronet;
}

// Transport failure returns Response{status=0, err=Error::kTransport} on both.
static bool check_failure_returns_kTransport() {
  muse::net::CronetConfig ok;
  muse::net::CronetConfig bad;
  bad.injectFailure = true;
  muse::net::CronetTransport cronetOk(ok);
  muse::net::CronetTransport cronetBad(bad);
  muse::net::ExecCtx ctx;
  muse::net::Response r = cronetBad.execute(makeGet(), ctx);
  if (r.status != 0 || r.err != muse::net::Error::kTransport) return false;
  muse::net::FakeDefaultTransport defBad(true /*fail*/);
  muse::net::Response d = defBad.execute(makeGet(), ctx);
  return d.status == 0 && d.err == muse::net::Error::kTransport;
}

// execute never throws, even on injected failure (error shape, not exception).
static bool check_execute_never_throws() {
  muse::net::CronetConfig bad;
  bad.injectFailure = true;
  muse::net::CronetTransport cronet(bad);
  muse::net::ExecCtx ctx;
  try {
    muse::net::Response r = cronet.execute(makeGet(), ctx);
    (void)r;
  } catch (...) {
    return false;
  }
  return true;
}

// name() identifies the transport distinctly from the default.
static bool check_name_identifies_cronet() {
  muse::net::CronetConfig cfg;
  muse::net::CronetTransport cronet(cfg);
  muse::net::FakeDefaultTransport def;
  std::string n = cronet.name();
  if (n.empty() || n == def.name()) return false;
  return n.find("cronet") != std::string::npos;
}

// Oversized/malformed URL fails closed with kTransport, never throws.
static bool check_malformed_url_fails_closed() {
  muse::net::CronetConfig cfg;
  muse::net::CronetTransport cronet(cfg);
  muse::net::ExecCtx ctx;
  muse::net::Request r;
  r.method = "GET";
  r.url = std::string(65536, 'x');
  try {
    muse::net::Response resp = cronet.execute(r, ctx);
    return resp.status == 0 && resp.err == muse::net::Error::kTransport;
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
  RUN_CASE(check_same_api_get_post_identical);
  RUN_CASE(check_interceptor_order_identical);
  RUN_CASE(check_failure_returns_kTransport);
  RUN_CASE(check_execute_never_throws);
  RUN_CASE(check_name_identifies_cronet);
  RUN_CASE(check_malformed_url_fails_closed);
  if (failures > 0) {
    std::printf("cronet_api_test: %d failing\n", failures);
    return 1;
  }
  std::printf("cronet_api_test: all passing\n");
  return 0;
}

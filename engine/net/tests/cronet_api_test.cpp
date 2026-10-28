// FR-NET-007 / AC-NET-010 (TC-NET-010) — CronetTransport on main's surface.
// Binds to main's ITransport/Request/Response/ExecCtx seam (net/transport.h,
// net/types.h): StatusCode (kOk, zero-cast failure), Error::kTransport,
// Origin{scheme,host,port}. Interceptor chaining lives on HttpClient
// (member AddInterceptor, outermost-registered runs first). The old header
// FakeDefaultTransport is gone: the default-transport double below is
// file-local to this test. Hermetic: no network, no env/config, no I/O.
#include <cstdio>
#include <memory>
#include <string>

#include "net/cronet_transport.h"
#include "net/http_client.h"
#include "net/interceptor.h"
#include "net/transport.h"
#include "net/types.h"

namespace {

constexpr muse::net::StatusCode kZeroStatus =
    static_cast<muse::net::StatusCode>(0);

// File-local default-transport double (mirrors the oracle behavior the
// reworked transport_policy.cc carries file-locally): fail-closed URL gate,
// "ok"/kOk/kNone on success, failure shape on injected failure, never throws.
class FakeDefaultTransport : public muse::net::ITransport {
 public:
  explicit FakeDefaultTransport(bool fail = false) : fail_(fail) {}

  muse::net::Response execute(const muse::net::Request& req,
                              const muse::net::ExecCtx&) override {
    muse::net::Response r;
    if (fail_ || !IsValid(req.url)) {
      r.status = kZeroStatus;
      r.err = muse::net::Error::kTransport;
      return r;
    }
    r.status = muse::net::StatusCode::kOk;
    r.body = "ok";
    r.err = muse::net::Error::kNone;
    return r;
  }

  std::string name() const override { return "default"; }

 private:
  static bool IsValid(const std::string& url) {
    if (url.empty() || url.size() > 8192) return false;
    return url.compare(0, 8, "https://") == 0 ||
           url.compare(0, 7, "http://") == 0;
  }

  bool fail_;
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

bool sameResponse(const muse::net::Response& a,
                  const muse::net::Response& b) {
  return a.status == b.status && a.body == b.body && a.err == b.err;
}

}  // namespace

// Same request sequence through the default double and CronetTransport yields
// identical application-visible results (transport-agnostic API).
static bool check_same_api_get_post_identical() {
  muse::net::CronetTransport cronet(muse::net::CronetConfig{});
  FakeDefaultTransport def;
  muse::net::ExecCtx ctx;
  if (!sameResponse(def.execute(makeGet(), ctx), cronet.execute(makeGet(), ctx))) {
    return false;
  }
  return sameResponse(def.execute(makePost(), ctx),
                      cronet.execute(makePost(), ctx));
}

// Interceptor chain (HttpClient member AddInterceptor) runs in identical order
// around either transport (outermost-registered runs first).
static bool check_interceptor_order_identical() {
  std::string orderDef;
  std::string orderCronet;
  muse::net::HttpClient defClient(
      std::make_shared<FakeDefaultTransport>(), muse::net::ClientConfig{});
  muse::net::HttpClient cronetClient(
      std::make_shared<muse::net::CronetTransport>(muse::net::CronetConfig{}),
      muse::net::ClientConfig{});
  defClient.AddInterceptor([&orderDef](muse::net::Request r, muse::net::Next n) {
    orderDef += "i1;";
    return n(r);
  });
  defClient.AddInterceptor([&orderDef](muse::net::Request r, muse::net::Next n) {
    orderDef += "i2;";
    return n(r);
  });
  cronetClient.AddInterceptor(
      [&orderCronet](muse::net::Request r, muse::net::Next n) {
        orderCronet += "i1;";
        return n(r);
      });
  cronetClient.AddInterceptor(
      [&orderCronet](muse::net::Request r, muse::net::Next n) {
        orderCronet += "i2;";
        return n(r);
      });
  const muse::net::Response a = defClient.request(makeGet());
  const muse::net::Response b = cronetClient.request(makeGet());
  if (a.err != muse::net::Error::kNone || b.err != muse::net::Error::kNone) {
    return false;
  }
  return !orderDef.empty() && orderDef == "i1;i2;" &&
         orderDef == orderCronet;
}

// Transport failure returns Response{zero status, err=Error::kTransport} both
// via injectFailure (cronet) and the fail flag (default double).
static bool check_failure_returns_kTransport() {
  muse::net::CronetConfig bad;
  bad.injectFailure = true;
  muse::net::CronetTransport cronetBad(bad);
  muse::net::ExecCtx ctx;
  muse::net::Response r = cronetBad.execute(makeGet(), ctx);
  if (r.status != kZeroStatus || r.err != muse::net::Error::kTransport) {
    return false;
  }
  FakeDefaultTransport defBad(true /*fail*/);
  muse::net::Response d = defBad.execute(makeGet(), ctx);
  return d.status == kZeroStatus && d.err == muse::net::Error::kTransport;
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
  muse::net::CronetTransport cronet(muse::net::CronetConfig{});
  FakeDefaultTransport def;
  const std::string n = cronet.name();
  if (n.empty() || n == def.name()) return false;
  return n.find("cronet") != std::string::npos;
}

// Oversized/empty/non-http(s) URLs fail closed with kTransport, never throw.
static bool check_malformed_url_fails_closed() {
  muse::net::CronetTransport cronet(muse::net::CronetConfig{});
  muse::net::ExecCtx ctx;
  const std::string badUrls[] = {
      std::string(65536, 'x'),
      "",
      "ftp://api.test.invalid/items",
  };
  for (const auto& url : badUrls) {
    muse::net::Request r;
    r.method = "GET";
    r.url = url;
    try {
      const muse::net::Response resp = cronet.execute(r, ctx);
      if (resp.status != kZeroStatus ||
          resp.err != muse::net::Error::kTransport) {
        return false;
      }
    } catch (...) {
      return false;
    }
  }
  return true;
}

#define RUN_CASE(name)                         \
  do {                                         \
    if (!(name)()) {                           \
      std::printf("FAIL %s\n", #name);         \
      ++failures;                              \
    } else {                                   \
      std::printf("PASS %s\n", #name);         \
    }                                          \
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

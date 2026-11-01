// Batch 3 contract tests — T6 (FR-NET-014 / AC-NET-027, TC-NET-027).
// Cronet is the default transport; BoringSSL remains selectable per
// build/init choice with no application code change (AC-NET-002 preserved).
//
// Binds ONLY to the T6 `Interface:` line (plan.md, verbatim):
//   DefaultTransportName(): std::string;
//   SelectTransport(name: std::string, client: HttpClient&): void
// plus the existing HttpClient/ITransport seam (net/transport.h,
// net/types.h) that SelectTransport binds.
//
// C++-spelling ASSUMPTIONS (reconciliation rule: update call sites, not
// expectations, if the coder names these differently):
//   A-NS:    both functions live in muse::net and are declared by
//            net/transport_config.h.
//   A-NAME:  the BoringSSL selection string is "boringssl". The plan names
//            no literal; "cronet" is fixed by DefaultTransportName(), and
//            "boringssl" is the symmetric, most defensible spelling.
//            NEEDS PLANNER CONFIRMATION — if the coder picks another
//            literal, update the call sites, not the expectations.
//   A-THROW: unknown/empty/hostile selection names throw (fail closed;
//            exact type not pinned — the test accepts any exception) and
//            leave the previously bound transport in place. Rationale: the
//            repo-wide fail-closed rule (fail-closed URL gates, fail-closed
//            pins); silently binding the wrong transport would violate
//            AC-NET-027's "selectable per build choice".
//   A-BORINGSSL-OK: the BoringSSL transport answers a valid https URL
//            in-process with err == kNone (same stub semantics as
//            CronetTransport's "ok"). Only err/status are asserted
//            literally for boringssl; body equality across transports is
//            asserted on status+err, not bytes.
// Hermetic: C++17 + committed net headers only. Transports observed are the
// real CronetTransport (injectFailure=false) and file-local dummies; no
// sockets, no TLS, no env/config reads, no filesystem, no live services.
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "net/cronet_transport.h"
#include "net/transport.h"
#include "net/transport_config.h"
#include "net/types.h"

namespace {

using muse::net::ClientConfig;
using muse::net::Error;
using muse::net::ExecCtx;
using muse::net::HttpClient;
using muse::net::ITransport;
using muse::net::Request;
using muse::net::Response;

constexpr muse::net::StatusCode kZeroStatus =
    static_cast<muse::net::StatusCode>(0);

// File-local always-failing dummy: proves SelectTransport actually rebinds
// (before selection every request is kTransport; after selection the bound
// production transport serves).
class AlwaysFail : public ITransport {
 public:
  Response execute(const Request&, const ExecCtx&) override {
    Response r;
    r.status = kZeroStatus;
    r.body = "";
    r.err = Error::kTransport;
    return r;
  }
  std::string name() const override { return "test-always-fail"; }
};

Request makeGet() {
  Request r;
  r.method = "GET";
  r.url = "https://api.test.invalid/items";
  return r;
}

Request makePost() {
  Request r;
  r.method = "POST";
  r.url = "https://api.test.invalid/items";
  r.body = "{\"a\":1}";
  r.headers.emplace_back("x-dummy-key", "test-api-key-not-real");
  return r;
}

// ---- Default ------------------------------------------------------------

// AC-NET-027: the default transport name is cronet.
bool check_default_transport_name_is_cronet() {
  return muse::net::DefaultTransportName() == "cronet";
}

// Pure query: repeated calls agree.
bool check_default_transport_name_is_stable() {
  return muse::net::DefaultTransportName() ==
         muse::net::DefaultTransportName();
}

// ---- Selection ----------------------------------------------------------

// Selecting the default name binds a serving transport: the client goes
// from always-failing to serving without any other code change.
bool check_select_default_name_binds_serving_transport() {
  HttpClient c(std::make_shared<AlwaysFail>(), ClientConfig{});
  if (c.request(makeGet()).err != Error::kTransport) return false;
  muse::net::SelectTransport(muse::net::DefaultTransportName(), c);
  const Response got = c.request(makeGet());
  return got.err == Error::kNone &&
         got.status == muse::net::StatusCode::kOk && got.body == "ok";
}

// A-NAME: "boringssl" selects the BoringSSL fallback and serves with the
// identical request API (AC-NET-002 preserved).
bool check_select_boringssl_serves_with_same_api() {
  HttpClient c(std::make_shared<AlwaysFail>(), ClientConfig{});
  muse::net::SelectTransport("boringssl", c);
  const Response get = c.request(makeGet());
  if (get.err != Error::kNone) return false;
  const Response post = c.request(makePost());
  return post.err == Error::kNone;
}

// AC-NET-002 preserved: the same application-visible request sequence
// yields the same status/err through the cronet-selected and the
// boringssl-selected client (no app code change between transports).
bool check_same_sequence_identical_on_both_transports() {
  HttpClient cronetClient(std::make_shared<AlwaysFail>(), ClientConfig{});
  HttpClient boringClient(std::make_shared<AlwaysFail>(), ClientConfig{});
  muse::net::SelectTransport(muse::net::DefaultTransportName(), cronetClient);
  muse::net::SelectTransport("boringssl", boringClient);
  const Request seq[] = {makeGet(), makePost(), makeGet()};
  for (const auto& q : seq) {
    const Response a = cronetClient.request(q);
    const Response b = boringClient.request(q);
    if (a.err != Error::kNone || b.err != Error::kNone) return false;
    if (a.status != b.status || a.err != b.err) return false;
  }
  return true;
}

// Selection is repeatable: selecting twice keeps serving.
bool check_reselect_is_idempotent() {
  HttpClient c(std::make_shared<AlwaysFail>(), ClientConfig{});
  muse::net::SelectTransport("boringssl", c);
  muse::net::SelectTransport(muse::net::DefaultTransportName(), c);
  return c.request(makeGet()).err == Error::kNone;
}

// ---- Fail-closed selection (security) -----------------------------------

// A-THROW: an unknown name throws and the previous binding keeps serving.
bool check_unknown_name_throws_and_keeps_prior_binding() {
  HttpClient c(std::make_shared<AlwaysFail>(), ClientConfig{});
  muse::net::SelectTransport(muse::net::DefaultTransportName(), c);
  bool threw = false;
  try {
    muse::net::SelectTransport("quic-photon-torbo", c);
  } catch (...) {
    threw = true;
  }
  if (!threw) return false;
  return c.request(makeGet()).err == Error::kNone;
}

// A-THROW: the empty name throws and never binds.
bool check_empty_name_throws_and_never_binds() {
  HttpClient c(std::make_shared<AlwaysFail>(), ClientConfig{});
  bool threw = false;
  try {
    muse::net::SelectTransport("", c);
  } catch (...) {
    threw = true;
  }
  if (!threw) return false;
  return c.request(makeGet()).err == Error::kTransport;
}

// Hostile selection strings (traversal, oversized, embedded NUL) throw and
// the client keeps serving on its prior binding; nothing escapes as a
// wrong-transport bind.
bool check_hostile_names_throw_and_client_still_serves() {
  HttpClient c(std::make_shared<AlwaysFail>(), ClientConfig{});
  muse::net::SelectTransport(muse::net::DefaultTransportName(), c);
  const std::string hostile[] = {
      std::string("../../etc/cronet"),
      std::string(1 << 20, 'c'),
      std::string("cronet\0boringssl", 17),
  };
  for (const auto& name : hostile) {
    bool threw = false;
    try {
      muse::net::SelectTransport(name, c);
    } catch (...) {
      threw = true;
    }
    if (!threw) return false;
    if (c.request(makeGet()).err != Error::kNone) return false;
  }
  return true;
}

// ---- Fail-closed requests after selection --------------------------------

// Empty URL after selection maps to kTransport, never throws (T1 seam
// contract carries over to the default transport).
bool check_empty_url_after_select_is_kTransport() {
  HttpClient c(std::make_shared<AlwaysFail>(), ClientConfig{});
  muse::net::SelectTransport(muse::net::DefaultTransportName(), c);
  Request r = makeGet();
  r.url = "";
  try {
    return c.request(r).err == Error::kTransport;
  } catch (...) {
    return false;
  }
}

}  // namespace

#define RUN_CASE(name)                   \
  do {                                   \
    if (!(name)()) {                     \
      std::printf("FAIL %s\n", #name);   \
      ++failures;                        \
    } else {                             \
      std::printf("PASS %s\n", #name);   \
    }                                    \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_default_transport_name_is_cronet);
  RUN_CASE(check_default_transport_name_is_stable);
  RUN_CASE(check_select_default_name_binds_serving_transport);
  RUN_CASE(check_select_boringssl_serves_with_same_api);
  RUN_CASE(check_same_sequence_identical_on_both_transports);
  RUN_CASE(check_reselect_is_idempotent);
  RUN_CASE(check_unknown_name_throws_and_keeps_prior_binding);
  RUN_CASE(check_empty_name_throws_and_never_binds);
  RUN_CASE(check_hostile_names_throw_and_client_still_serves);
  RUN_CASE(check_empty_url_after_select_is_kTransport);
  if (failures > 0) {
    std::printf("cronet_default_test: %d failing\n", failures);
    return 1;
  }
  std::printf("cronet_default_test: all passing\n");
  return 0;
}

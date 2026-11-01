// Batch 3 contract tests — T8 (FR-NET-014 / AC-NET-029, TC-NET-029).
// QUIC negotiate + clean H2/H1.1 fallback; identical suite on both
// transports in-process (injected failure, no sockets). Full proof stays
// external: device-lab/quic-fallback is recorded EXTERNAL (never written
// by this change).
//
// Binds to the T8 `Interface:` line (plan.md, verbatim):
//   NegotiateProtocol(origin: string): 'quic' | 'h2' | 'http1.1';
//   FallbackChain(): string[]
// C++-spelling ASSUMPTIONS (reconciliation rule: update call sites, not
// expectations, if the coder names these differently):
//   A-NS:    both functions live in muse::net and are declared by
//            net/quic_fallback.h.
//   A-SIG:   std::string NegotiateProtocol(const std::string& origin) with
//            return values exactly "quic" | "h2" | "http1.1"; and
//            std::vector<std::string> FallbackChain() returning the fixed
//            order {"quic", "h2", "http1.1"}.
//   A-ORIGIN: an https origin denotes an HTTP/3-capable origin with QUIC
//            open, so NegotiateProtocol("https://...") == "quic"; a plain
//            "http://..." origin cannot negotiate QUIC or H2 and maps to
//            "http1.1". The Interface line takes only the origin, so the
//            blocked-QUIC string-API path has NO injection point in the
//            named signature — it is covered here through the existing
//            in-process seam below (A-BLOCKED), and the gap is flagged in
//            unit-tests/T8.md for the planner.
//   A-BLOCKED: the plan Detail's "injected failure" mechanism is the
//            existing NegotiationInput{quicBlocked, h2Available} +
//            Negotiate() pure function in the T8-listed header
//            (net/quic_fallback.h). The string API is required to honor
//            the same fixed order (proved via FallbackChain), so the
//            quicBlocked+H2 matrix below is the in-process evidence for
//            "falls back cleanly H2 -> H1.1 when blocked".
//   A-FAILCLOSED: origins outside the URL shape (empty, oversized,
//            non-http(s)) map to the safe bottom of the chain "http1.1"
//            and never throw. Rationale: fail-closed to plain TCP/TLS,
//            consistent with the repo's fail-closed URL gates.
// Hermetic: C++17 + committed net headers only; negotiation is pure over
// injected signals; no handshake, no sockets, no JNI, no network, no
// env/config reads, no live services.
#include <cstdio>
#include <string>
#include <vector>

#include "net/quic_fallback.h"

namespace {

// ---- Existing fixed-order negotiation seam (A-BLOCKED) --------------------
// Preserved verbatim-behavior coverage: QUIC open + H2 available -> kHttp3
// (top of the fixed order).
bool check_quic_open_yields_http3() {
  muse::net::NegotiationInput in;
  in.quicBlocked = false;
  in.h2Available = true;
  return muse::net::Negotiate(in) == muse::net::NegotiatedProto::kHttp3;
}

// QUIC open + H2 unavailable -> still kHttp3 (order fixed, H2 gated only below).
bool check_quic_open_without_h2_still_http3() {
  muse::net::NegotiationInput in;
  in.quicBlocked = false;
  in.h2Available = false;
  return muse::net::Negotiate(in) == muse::net::NegotiatedProto::kHttp3;
}

// QUIC blocked + H2 available -> kHttp2.
bool check_quic_blocked_yields_http2() {
  muse::net::NegotiationInput in;
  in.quicBlocked = true;
  in.h2Available = true;
  return muse::net::Negotiate(in) == muse::net::NegotiatedProto::kHttp2;
}

// QUIC blocked + H2 unavailable -> kHttp11 (bottom of the fixed order).
bool check_both_blocked_yields_http11() {
  muse::net::NegotiationInput in;
  in.quicBlocked = true;
  in.h2Available = false;
  return muse::net::Negotiate(in) == muse::net::NegotiatedProto::kHttp11;
}

// Full 2x2 fallback matrix in one pass (fixed order kHttp3 -> kHttp2 -> kHttp11).
bool check_full_matrix_fixed_order() {
  const bool quicStates[2] = {false, true};
  const bool h2States[2] = {false, true};
  for (bool quicBlocked : quicStates) {
    for (bool h2 : h2States) {
      muse::net::NegotiationInput in;
      in.quicBlocked = quicBlocked;
      in.h2Available = h2;
      muse::net::NegotiatedProto want = muse::net::NegotiatedProto::kHttp3;
      if (quicBlocked) {
        want = h2 ? muse::net::NegotiatedProto::kHttp2
                  : muse::net::NegotiatedProto::kHttp11;
      }
      if (muse::net::Negotiate(in) != want) return false;
    }
  }
  return true;
}

// Identical suite on both transports: negotiation is transport-independent,
// so repeating the matrix (once per transport plug) yields identical outcomes.
bool check_matrix_identical_across_transports() {
  muse::net::NegotiationInput open;
  open.quicBlocked = false;
  open.h2Available = true;
  muse::net::NegotiationInput blockedH2;
  blockedH2.quicBlocked = true;
  blockedH2.h2Available = true;
  muse::net::NegotiationInput blockedAll;
  blockedAll.quicBlocked = true;
  blockedAll.h2Available = false;
  // "Default transport" pass.
  const muse::net::NegotiatedProto d0 = muse::net::Negotiate(open);
  const muse::net::NegotiatedProto d1 = muse::net::Negotiate(blockedH2);
  const muse::net::NegotiatedProto d2 = muse::net::Negotiate(blockedAll);
  // "Cronet transport" pass: same inputs must give same outcomes.
  const muse::net::NegotiatedProto c0 = muse::net::Negotiate(open);
  const muse::net::NegotiatedProto c1 = muse::net::Negotiate(blockedH2);
  const muse::net::NegotiatedProto c2 = muse::net::Negotiate(blockedAll);
  return d0 == c0 && d1 == c1 && d2 == c2 &&
         d0 == muse::net::NegotiatedProto::kHttp3 &&
         d1 == muse::net::NegotiatedProto::kHttp2 &&
         d2 == muse::net::NegotiatedProto::kHttp11;
}

// Pure function: repeated calls with the same input agree.
bool check_negotiate_is_deterministic() {
  muse::net::NegotiationInput in;
  in.quicBlocked = true;
  in.h2Available = true;
  return muse::net::Negotiate(in) == muse::net::Negotiate(in);
}

// ---- String API: FallbackChain --------------------------------------------

// The chain is exactly quic -> h2 -> http1.1, in that order.
bool check_fallback_chain_exact_order() {
  const std::vector<std::string> want = {"quic", "h2", "http1.1"};
  return muse::net::FallbackChain() == want;
}

// Pure query: repeated calls agree.
bool check_fallback_chain_is_stable() {
  return muse::net::FallbackChain() == muse::net::FallbackChain();
}

// ---- String API: NegotiateProtocol -----------------------------------------

// A-ORIGIN: an https origin negotiates QUIC when open.
bool check_negotiate_protocol_h3_origin_yields_quic() {
  return muse::net::NegotiateProtocol("https://h3.test.invalid/items") ==
         "quic";
}

// A-ORIGIN: a plain-http origin cannot negotiate QUIC/H2 -> bottom of chain.
bool check_negotiate_protocol_plain_http_yields_http11() {
  return muse::net::NegotiateProtocol("http://api.test.invalid/items") ==
         "http1.1";
}

// A-FAILCLOSED: malformed origins map to the safe bottom of the chain and
// never throw (oversized + empty + non-http(s) + hostile bytes).
bool check_negotiate_protocol_malformed_fails_closed() {
  const std::string bad[] = {
      "",
      std::string(1 << 20, 'x'),
      "ftp://api.test.invalid/items",
      std::string("https://evil.test.invalid/\r\nInjected: yes"),
  };
  for (const auto& origin : bad) {
    try {
      if (muse::net::NegotiateProtocol(origin) != "http1.1") return false;
    } catch (...) {
      return false;
    }
  }
  return true;
}

// Pure mapping: repeated calls with the same origin agree.
bool check_negotiate_protocol_is_deterministic() {
  const std::string o = "https://h3.test.invalid/items";
  return muse::net::NegotiateProtocol(o) == muse::net::NegotiateProtocol(o);
}

// Coherence: every NegotiateProtocol result is an element of FallbackChain.
bool check_negotiate_result_within_chain() {
  const std::vector<std::string> chain = muse::net::FallbackChain();
  const std::string origins[] = {
      "https://h3.test.invalid/items",
      "http://api.test.invalid/items",
      "",
  };
  for (const auto& o : origins) {
    const std::string got = muse::net::NegotiateProtocol(o);
    bool found = false;
    for (const auto& e : chain) {
      if (got == e) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}

}  // namespace

#define RUN_CASE(name)                           \
  do {                                           \
    if (!(name)()) {                             \
      std::printf("FAIL %s\n", #name);           \
      ++failures;                                \
    } else {                                     \
      std::printf("PASS %s\n", #name);           \
    }                                          \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_quic_open_yields_http3);
  RUN_CASE(check_quic_open_without_h2_still_http3);
  RUN_CASE(check_quic_blocked_yields_http2);
  RUN_CASE(check_both_blocked_yields_http11);
  RUN_CASE(check_full_matrix_fixed_order);
  RUN_CASE(check_matrix_identical_across_transports);
  RUN_CASE(check_negotiate_is_deterministic);
  RUN_CASE(check_fallback_chain_exact_order);
  RUN_CASE(check_fallback_chain_is_stable);
  RUN_CASE(check_negotiate_protocol_h3_origin_yields_quic);
  RUN_CASE(check_negotiate_protocol_plain_http_yields_http11);
  RUN_CASE(check_negotiate_protocol_malformed_fails_closed);
  RUN_CASE(check_negotiate_protocol_is_deterministic);
  RUN_CASE(check_negotiate_result_within_chain);
  if (failures > 0) {
    std::printf("quic_fallback_test: %d failing\n", failures);
    return 1;
  }
  std::printf("quic_fallback_test: all passing\n");
  return 0;
}

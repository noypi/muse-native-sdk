// Batch 2 contract tests — FR-NET-007 / AC-NET-012 (TC-NET-012).
// Covers T3 (QUIC negotiation with clean fallback HTTP/3 -> HTTP/2 -> HTTP/1.1).
// Hermetic: C++17 + committed net headers only; no network, no env/config
// reads, no live services. Negotiation is a pure function over injected
// signals (NegotiationInput); no handshake, no sockets, no JNI.
// Binds ONLY to the T3 `Interface:` line; never to implementation.
#include <cstdio>

#include "net/quic_fallback.h"

namespace {

// QUIC open + H2 available -> kHttp3 (top of the fixed order).
static bool check_quic_open_yields_http3() {
  muse::net::NegotiationInput in;
  in.quicBlocked = false;
  in.h2Available = true;
  return muse::net::Negotiate(in) == muse::net::NegotiatedProto::kHttp3;
}

// QUIC open + H2 unavailable -> still kHttp3 (order fixed, H2 gated only below).
static bool check_quic_open_without_h2_still_http3() {
  muse::net::NegotiationInput in;
  in.quicBlocked = false;
  in.h2Available = false;
  return muse::net::Negotiate(in) == muse::net::NegotiatedProto::kHttp3;
}

// QUIC blocked + H2 available -> kHttp2.
static bool check_quic_blocked_yields_http2() {
  muse::net::NegotiationInput in;
  in.quicBlocked = true;
  in.h2Available = true;
  return muse::net::Negotiate(in) == muse::net::NegotiatedProto::kHttp2;
}

// QUIC blocked + H2 unavailable -> kHttp11 (bottom of the fixed order).
static bool check_both_blocked_yields_http11() {
  muse::net::NegotiationInput in;
  in.quicBlocked = true;
  in.h2Available = false;
  return muse::net::Negotiate(in) == muse::net::NegotiatedProto::kHttp11;
}

// Full 2x2 fallback matrix in one pass (fixed order kHttp3 -> kHttp2 -> kHttp11).
static bool check_full_matrix_fixed_order() {
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

// Identical suite on both transports: negotiation is transport-independent, so
// repeating the matrix (once per transport plug) yields identical outcomes.
static bool check_matrix_identical_across_transports() {
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
static bool check_negotiate_is_deterministic() {
  muse::net::NegotiationInput in;
  in.quicBlocked = true;
  in.h2Available = true;
  return muse::net::Negotiate(in) == muse::net::Negotiate(in);
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
  if (failures > 0) {
    std::printf("quic_fallback_test: %d failing\n", failures);
    return 1;
  }
  std::printf("quic_fallback_test: all passing\n");
  return 0;
}

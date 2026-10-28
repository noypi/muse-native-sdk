#pragma once

namespace muse::net {

// Negotiated application protocol, in fixed fallback order
// kHttp3 -> kHttp2 -> kHttp11.
enum class NegotiatedProto {
  kHttp3,
  kHttp2,
  kHttp11,
};

// Injected negotiation signals. Pure input: no handshake, no sockets, no JNI.
struct NegotiationInput {
  bool quicBlocked;
  bool h2Available;
};

// Fixed-order negotiation: QUIC open yields kHttp3; QUIC blocked falls back to
// kHttp2 when available, else kHttp11. Pure and deterministic.
NegotiatedProto Negotiate(NegotiationInput in);

}  // namespace muse::net

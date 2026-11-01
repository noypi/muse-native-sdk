#pragma once

#include <string>
#include <vector>

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

// Fixed fallback order, top to bottom: quic -> h2 -> http1.1.
std::vector<std::string> FallbackChain();

// String API over an origin URL. "https://" origins negotiate "quic";
// anything else (including "http://", empty, oversized, non-http(s), or
// CRLF/control-byte origins) fails closed to "http1.1". Never throws.
std::string NegotiateProtocol(const std::string& origin);

}  // namespace muse::net

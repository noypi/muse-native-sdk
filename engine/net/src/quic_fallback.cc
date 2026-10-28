#include "net/quic_fallback.h"

namespace muse::net {

NegotiatedProto Negotiate(NegotiationInput in) {
  if (!in.quicBlocked) {
    return NegotiatedProto::kHttp3;
  }
  if (in.h2Available) {
    return NegotiatedProto::kHttp2;
  }
  return NegotiatedProto::kHttp11;
}

}  // namespace muse::net

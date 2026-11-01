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

std::vector<std::string> FallbackChain() {
  return {"quic", "h2", "http1.1"};
}

std::string NegotiateProtocol(const std::string& origin) {
  try {
    if (origin.empty() || origin.size() > 8192) {
      return "http1.1";
    }
    for (char c : origin) {
      if (c == '\r' || c == '\n' || c == '\0') {
        return "http1.1";
      }
    }
    static const std::string kHttps = "https://";
    if (origin.size() > kHttps.size() &&
        origin.compare(0, kHttps.size(), kHttps) == 0) {
      return "quic";
    }
    return "http1.1";
  } catch (...) {
    return "http1.1";
  }
}

}  // namespace muse::net

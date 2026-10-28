#pragma once

#include <memory>
#include <string>

#include "net/cronet_transport.h"
#include "net/transport.h"

namespace muse::net {

// Reuses main's Origin (net/types.h: scheme/host/port) so policy keys on the
// same origin shape as the connection pool; the port is ignored by policy.

// TLS policy seam (SEC-NET owned; consumed only). Deny-by-default: any pinning
// failure or cleartext denial fails closed with Error::kTransport before any
// socket use, identically on both transports. (Main's Error vocabulary has no
// distinct policy-denied enumerator, so denial is reported as failure to
// execute — err == kTransport with zero status — per the transport contract.)
class ITlsPolicy {
 public:
  virtual ~ITlsPolicy() = default;
  virtual bool isPinned(const Origin& origin) const = 0;
  virtual bool isCleartextAllowed(const Origin& origin) const = 0;
};

// QUIC socket factory seam. Carried for init-time plumbing; policy denial
// returns before any socket use, so the factory is never touched on deny.
class IQuicSocketFactory {
 public:
  virtual ~IQuicSocketFactory() = default;
};

// Build/init-time transport selection, consumed from the muse-native.yaml key
// by plumbing only.
enum class TransportKind {
  kDefault,
  kCronet,
};

struct AppNetConfig {
  TransportKind transport = TransportKind::kDefault;
};

// Creates the requested transport wrapped with the shared policy gate. Never
// throws; a denied request returns Response{status=kUnknown,
// err=Error::kTransport} before any socket use.
std::shared_ptr<ITransport> CreateTransport(const AppNetConfig& config,
                                            ITlsPolicy& tls,
                                            IQuicSocketFactory* factory = nullptr);

}  // namespace muse::net

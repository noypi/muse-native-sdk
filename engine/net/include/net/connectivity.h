#pragma once

#include <functional>
#include <string>

#include "net/transport_policy.h"

namespace muse::net {

// Connectivity state for FR-NET-013 / AC-NET-026. kOnlineUnvalidated covers
// the captive-portal case: link is up but validation has not succeeded, so
// prefetch stays suppressed until kOnlineValidated arrives.
enum class NetState {
  kOffline,
  kOnlineUnvalidated,
  kOnlineValidated,
};

struct NetInfo {
  NetState state = NetState::kOffline;
  bool metered = false;
  bool vpn = false;
};

using NetSignal = std::function<void(NetInfo)>;

// Network security config seam (SEC-NET owned; consumed only). The monitor
// never inspects policy internals; deny-by-default lives in the config.
class INetworkSecurityConfig {
 public:
  virtual ~INetworkSecurityConfig() = default;
  virtual bool cleartextPermitted(const std::string& domain) const = 0;
};

// In-process connectivity monitor. Thresholds arrive as injected NetInfo
// values via onPlatformEvent (ConnectivityManager is faked, never touched);
// `vpn` is carried as orthogonal signal data and never gates prefetch.
class ConnectivityMonitor {
 public:
  void setListener(NetSignal s);
  void onPlatformEvent(NetInfo i);
  bool shouldPrefetch(NetInfo i) const;
  bool isCleartextAllowed(const Origin& origin,
                          const INetworkSecurityConfig& config) const;

 private:
  NetSignal listener_;
};

}  // namespace muse::net

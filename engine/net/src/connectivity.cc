#include "net/connectivity.h"

#include <utility>

namespace muse::net {

void ConnectivityMonitor::setListener(NetSignal s) {
  listener_ = std::move(s);
}

void ConnectivityMonitor::onPlatformEvent(NetInfo i) {
  if (listener_) {
    listener_(i);
  }
}

bool ConnectivityMonitor::shouldPrefetch(NetInfo i) const {
  return i.state == NetState::kOnlineValidated && !i.metered;
}

bool ConnectivityMonitor::isCleartextAllowed(
    const Origin& origin, const INetworkSecurityConfig& config) const {
  try {
    return config.cleartextPermitted(origin.host);
  } catch (...) {
    return false;
  }
}

}  // namespace muse::net

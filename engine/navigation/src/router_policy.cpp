#include "navigation/router_policy.h"

#include <cstdlib>
#include <string>

// See header for the flag contract (name, default ON, green criteria).
bool IsArbitraryRouteEnabled() {
  const char* env = std::getenv("MUSE_ARBITRARY_ROUTE_ENABLED");
  if (env != nullptr) {
    return std::string(env) != "0";
  }
#ifdef MUSE_ARBITRARY_ROUTE_ENABLED
#define MUSE_ROUTE_FLAG_STR_(x) #x
#define MUSE_ROUTE_FLAG_STR(x) MUSE_ROUTE_FLAG_STR_(x)
  return std::string(MUSE_ROUTE_FLAG_STR(MUSE_ARBITRARY_ROUTE_ENABLED)) != "0";
#undef MUSE_ROUTE_FLAG_STR
#undef MUSE_ROUTE_FLAG_STR_
#else
  return true;
#endif
}

RouterPolicy::RouterPolicy(std::vector<std::string> routes)
    : routes_(std::move(routes)) {}

PolicyVerdict RouterPolicy::checkDestination(const std::string& routeId) const {
  for (const auto& route : routes_) {
    if (route == routeId) {
      return {true, PolicyError::NONE};
    }
  }
  if (!routeId.empty() &&
      routeId.find('/') != std::string::npos) {
    return {false, PolicyError::BYPASS_ATTEMPT};
  }
  return {false, PolicyError::UNREGISTERED_ROUTE};
}

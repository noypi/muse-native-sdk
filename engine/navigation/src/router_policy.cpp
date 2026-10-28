#include "navigation/router_policy.h"

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

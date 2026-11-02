#include "navigation/router_facade_gate.h"

RouterFacadeGate::RouterFacadeGate(std::vector<RouteDescriptor> descriptors)
    : descriptors_(std::move(descriptors)) {}

void RouterFacadeGate::setGuardHook(GuardFn guard) {
  guard_ = std::move(guard);
}

GateResult RouterFacadeGate::commit(
    const std::string& route,
    const std::map<std::string, TypedParam>& params) const {
  const RouteDescriptor* found = nullptr;
  for (const RouteDescriptor& desc : descriptors_) {
    if (desc.routeId == route) {
      found = &desc;
      break;
    }
  }
  if (found == nullptr) {
    return GateResult{false, GateCode::UNKNOWN_ROUTE};
  }
  if (!validateParams(*found, params).ok) {
    return GateResult{false, GateCode::BAD_PARAMS};
  }
  if (guard_ && !guard_(route)) {
    return GateResult{false, GateCode::GUARD_DENIED};
  }
  return GateResult{true, GateCode::OK};
}

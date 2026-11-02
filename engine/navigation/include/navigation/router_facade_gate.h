#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "navigation/snapshot.h"
#include "navigation/trie.h"
#include "navigation/typed_routes.h"

enum class GateCode {
  OK,
  UNKNOWN_ROUTE,
  BAD_PARAMS,
  GUARD_DENIED,
};

struct GateResult {
  bool ok = false;
  GateCode code = GateCode::UNKNOWN_ROUTE;
};

// C++ commit gate for the nav-router facade (FR-NAV-014 / AC-NAV-042).
// Validates route + typed params against the descriptor table, then re-runs
// the guard hook; any denial fails closed and never commits navigation.
class RouterFacadeGate {
 public:
  explicit RouterFacadeGate(std::vector<RouteDescriptor> descriptors);

  void setGuardHook(GuardFn guard);

  GateResult commit(const std::string& route,
                    const std::map<std::string, TypedParam>& params) const;

 private:
  std::vector<RouteDescriptor> descriptors_;
  GuardFn guard_;
};

#pragma once

#include <string>
#include <vector>

enum class PolicyError {
  NONE,
  BYPASS_ATTEMPT,
  UNREGISTERED_ROUTE,
};

struct PolicyVerdict {
  bool allowed = false;
  PolicyError error = PolicyError::UNREGISTERED_ROUTE;
};

// Single-router enforcement (FR-NAV-002 / AC-NAV-005). The policy owns a
// registry of graph-owned route ids; checkDestination allows registered
// ids and denies everything else. Ids containing '/' denote imperative
// platform-component starts bypassing the graph and yield BYPASS_ATTEMPT;
// other unknown (incl. empty) ids yield UNREGISTERED_ROUTE. The check is
// const and mutates nothing.
// Rollout flag gate (FR-NAV-013 / AC-NAV-037, TC-NAV-037).
// Flag name: MUSE_ARBITRARY_ROUTE_ENABLED (env var, or compile-time define
// -DMUSE_ARBITRARY_ROUTE_ENABLED=<value> as the build-level equivalent).
// Default: ON (gate open; arbitrary-route navigate allowed). Returns false
// only when rollback is explicitly requested, i.e. the flag value is "0".
// Any other value (including "1" or unset) leaves the gate open.
// Green criteria: TC-NAV-033 + TC-NAV-035 + TC-NAV-036 green (T1/T3/T5
// suites stay green; this gate is default-open so it never breaks them).
bool IsArbitraryRouteEnabled();

class RouterPolicy {
 public:
  explicit RouterPolicy(std::vector<std::string> routes);

  PolicyVerdict checkDestination(const std::string& routeId) const;

 private:
  std::vector<std::string> routes_;
};

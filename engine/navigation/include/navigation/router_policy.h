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
class RouterPolicy {
 public:
  explicit RouterPolicy(std::vector<std::string> routes);

  PolicyVerdict checkDestination(const std::string& routeId) const;

 private:
  std::vector<std::string> routes_;
};

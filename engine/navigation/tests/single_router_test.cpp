// T5 contract tests — FR-NAV-002 / AC-NAV-005 (TC-NAV-005).
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services. Consults no implementation;
// binds to the T5 `Interface:` line verbatim (see unit-tests/T5.md).
//
// ASSUMPTION (construction): the plan `Interface:` line names no
// constructor. These cases construct the policy with the registered
// route table, `explicit RouterPolicy(std::vector<std::string> routes)`,
// mirroring T1's `NavGraph` convention. Update call sites, not
// expectations, if the coder names it differently.
// ASSUMPTION (bypass shape): `checkDestination` takes only a routeId, so
// an imperative Activity/Fragment start is expressed as a destination
// outside the graph registry. RouteIds containing '/' (e.g.
// "activity/Settings", "fragment/Profile") denote platform components
// started bypassing the graph and must yield BYPASS_ATTEMPT; other
// unregistered ids yield UNREGISTERED_ROUTE. If the coder adds a
// separate `notifyImperativeStart`-style seam, reconcile call sites.
#include <cstdio>
#include <string>
#include <vector>

#include "navigation/router_policy.h"

namespace {

int failures = 0;

#define EXPECT_TRUE(cond, name)                       \
  do {                                                \
    if (!(cond)) {                                    \
      std::printf("FAIL %s\n", name);                 \
      ++failures;                                     \
    } else {                                          \
      std::printf("PASS %s\n", name);                 \
    }                                                 \
  } while (0)

const std::vector<std::string> kRoutes = {"home", "details", "settings"};

bool check_registered_destination_allowed() {
  RouterPolicy p(kRoutes);
  PolicyVerdict v = p.checkDestination("details");
  return v.allowed && v.error == PolicyError::NONE;
}

bool check_home_destination_allowed() {
  RouterPolicy p(kRoutes);
  PolicyVerdict v = p.checkDestination("home");
  return v.allowed && v.error == PolicyError::NONE;
}

bool check_imperative_activity_start_denied_as_bypass() {
  RouterPolicy p(kRoutes);
  PolicyVerdict v = p.checkDestination("activity/Settings");
  return !v.allowed && v.error == PolicyError::BYPASS_ATTEMPT;
}

bool check_imperative_fragment_start_denied_as_bypass() {
  RouterPolicy p(kRoutes);
  PolicyVerdict v = p.checkDestination("fragment/Profile");
  return !v.allowed && v.error == PolicyError::BYPASS_ATTEMPT;
}

bool check_unregistered_route_denied() {
  RouterPolicy p(kRoutes);
  PolicyVerdict v = p.checkDestination("no_such_route");
  return !v.allowed && v.error == PolicyError::UNREGISTERED_ROUTE;
}

bool check_empty_route_id_denied() {
  RouterPolicy p(kRoutes);
  PolicyVerdict v = p.checkDestination("");
  return !v.allowed && v.error != PolicyError::NONE;
}

bool check_denied_destination_leaves_policy_unchanged() {
  RouterPolicy p(kRoutes);
  p.checkDestination("activity/Settings");
  PolicyVerdict v = p.checkDestination("details");
  return v.allowed && v.error == PolicyError::NONE;
}

}  // namespace

int main() {
  EXPECT_TRUE(check_registered_destination_allowed(),
              "registered_destination_allowed");
  EXPECT_TRUE(check_home_destination_allowed(), "home_destination_allowed");
  EXPECT_TRUE(check_imperative_activity_start_denied_as_bypass(),
              "imperative_activity_start_denied_as_bypass");
  EXPECT_TRUE(check_imperative_fragment_start_denied_as_bypass(),
              "imperative_fragment_start_denied_as_bypass");
  EXPECT_TRUE(check_unregistered_route_denied(), "unregistered_route_denied");
  EXPECT_TRUE(check_empty_route_id_denied(), "empty_route_id_denied");
  EXPECT_TRUE(check_denied_destination_leaves_policy_unchanged(),
              "denied_destination_leaves_policy_unchanged");
  if (failures > 0) {
    std::printf("single_router_test: %d failing\n", failures);
    return 1;
  }
  std::printf("single_router_test: all passing\n");
  return 0;
}

// T6 contract tests — FR-NAV-008 / AC-NAV-021 (TC-NAV-021).
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services.
// Binds to the plan T6 `Interface:` line (verbatim):
//   `TransitionSpec transitionFor(const std::string& routeId);`
//   `void setTransition(const std::string& routeId, const TransitionSpec& s);`
// where `TransitionSpec{enterMs:int, exitMs:int, popMs:int, easing:string}`
// with defaults `enter 300 / exit 250 / pop 250, easing "m3-emphasized"`
// from M3 tokens.
//
// ASSUMPTION (namespace): the plan names unqualified functions; tests call
// them in the global namespace. If the coder places them in a namespace,
// update the call sites — not the expected behavior.
// ASSUMPTION (negative/zero): rejected to M3 defaults per the plan Detail
// ("negative/zero durations rejected to defaults with no crash"); the test
// asserts the defaults are returned and the process survives.
#include <cstdio>
#include <string>

#include "navigation/route_transition.h"

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

bool check_declared_route_returns_configured_spec() {
  TransitionSpec s;
  s.enterMs = 400;
  s.exitMs = 200;
  s.popMs = 180;
  s.easing = "m3-emphasized";
  setTransition("profile", s);
  TransitionSpec got = transitionFor("profile");
  return got.enterMs == 400 && got.exitMs == 200 && got.popMs == 180 &&
         got.easing == "m3-emphasized";
}

bool check_undeclared_route_returns_m3_defaults() {
  TransitionSpec got = transitionFor("never-declared-route-xyz");
  return got.enterMs == 300 && got.exitMs == 250 && got.popMs == 250 &&
         got.easing == "m3-emphasized";
}

bool check_negative_durations_fall_back_to_defaults() {
  TransitionSpec s;
  s.enterMs = -100;
  s.exitMs = -50;
  s.popMs = -10;
  s.easing = "m3-emphasized";
  setTransition("bad-negative", s);
  TransitionSpec got = transitionFor("bad-negative");
  return got.enterMs == 300 && got.exitMs == 250 && got.popMs == 250 &&
         got.easing == "m3-emphasized";
}

bool check_zero_durations_fall_back_to_defaults() {
  TransitionSpec s;
  s.enterMs = 0;
  s.exitMs = 0;
  s.popMs = 0;
  s.easing = "m3-emphasized";
  setTransition("bad-zero", s);
  TransitionSpec got = transitionFor("bad-zero");
  return got.enterMs == 300 && got.exitMs == 250 && got.popMs == 250 &&
         got.easing == "m3-emphasized";
}

bool check_reconfigure_updates_spec() {
  TransitionSpec s;
  s.enterMs = 500;
  s.exitMs = 300;
  s.popMs = 300;
  s.easing = "m3-emphasized";
  setTransition("reconfig", s);
  TransitionSpec first = transitionFor("reconfig");
  s.enterMs = 600;
  setTransition("reconfig", s);
  TransitionSpec second = transitionFor("reconfig");
  return first.enterMs == 500 && second.enterMs == 600 &&
         second.exitMs == 300 && second.popMs == 300;
}

}  // namespace

int main() {
  EXPECT_TRUE(check_declared_route_returns_configured_spec(),
              "declared_route_returns_configured_spec");
  EXPECT_TRUE(check_undeclared_route_returns_m3_defaults(),
              "undeclared_route_returns_m3_defaults");
  EXPECT_TRUE(check_negative_durations_fall_back_to_defaults(),
              "negative_durations_fall_back_to_defaults_no_crash");
  EXPECT_TRUE(check_zero_durations_fall_back_to_defaults(),
              "zero_durations_fall_back_to_defaults_no_crash");
  EXPECT_TRUE(check_reconfigure_updates_spec(), "reconfigure_updates_spec");
  if (failures == 0) {
    std::printf("ALL PASS\n");
  }
  return failures == 0 ? 0 : 1;
}

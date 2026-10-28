#pragma once

#include <string>

struct TransitionSpec {
  int enterMs = 300;
  int exitMs = 250;
  int popMs = 250;
  std::string easing = "m3-emphasized";
};

// Per-route transition config (FR-NAV-008 / AC-NAV-021). Declared routes
// return their configured spec; undeclared routes fall back to M3 defaults
// (300/250/250, "m3-emphasized"). Non-positive durations are rejected to
// defaults with no crash.
TransitionSpec transitionFor(const std::string& routeId);
void setTransition(const std::string& routeId, const TransitionSpec& s);

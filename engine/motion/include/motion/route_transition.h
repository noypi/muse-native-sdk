#ifndef MOTION_ROUTE_TRANSITION_H_
#define MOTION_ROUTE_TRANSITION_H_

#include <map>
#include <string>
#include <utility>

#include "motion/implicit.h"

namespace motion {

// Navigation direction for a route transition (FR-MOTION-007 / AC-MOTION-016).
enum class Direction {
  Enter,
  Exit,
  Pop,
  Gesture,
};

// Per-route navigation transition config.
//
// Config-only layer: route ids are opaque strings with no routing behavior.
// Each configured (route, direction) pair resolves to its configured spec;
// any unconfigured pair falls back to the default spec. resolve() never
// throws and returns a stable reference.
class RouteTransitions {
 public:
  void configure(const std::string& route, Direction dir, TransitionSpec spec);
  const TransitionSpec& resolve(const std::string& route,
                                Direction dir) const noexcept;

 private:
  std::map<std::pair<std::string, Direction>, TransitionSpec> entries_;
  TransitionSpec defaultSpec_;
};

}  // namespace motion

#endif  // MOTION_ROUTE_TRANSITION_H_

#include "motion/route_transition.h"

namespace motion {

void RouteTransitions::configure(const std::string& route, Direction dir,
                                  TransitionSpec spec) {
  entries_[std::make_pair(route, dir)] = spec;
}

const TransitionSpec& RouteTransitions::resolve(
    const std::string& route, Direction dir) const noexcept {
  // Documented fallback: unconfigured (route, dir) pairs resolve to the
  // default spec. Lookup is keyed read-only; on any failure the stable
  // default member is returned so this never throws.
  try {
    auto it = entries_.find(std::make_pair(route, dir));
    if (it != entries_.end()) return it->second;
  } catch (...) {
  }
  return defaultSpec_;
}

}  // namespace motion

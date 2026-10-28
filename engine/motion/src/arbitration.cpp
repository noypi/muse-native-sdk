#include "motion/arbitration.h"

namespace motion {

Winner arbitrate(std::vector<Claim> claims) noexcept {
  if (claims.empty()) {
    Winner w;
    w.none = true;
    return w;
  }
  const Claim* best = &claims[0];
  for (std::size_t i = 1; i < claims.size(); ++i) {
    const Claim& c = claims[i];
    if (c.priority != best->priority) {
      if (c.priority > best->priority) best = &c;
    } else if (c.startMs != best->startMs) {
      if (c.startMs > best->startMs) best = &c;
    } else if (c.id < best->id) {
      best = &c;
    }
  }
  Winner w;
  w.id = best->id;
  w.none = false;
  return w;
}

}  // namespace motion

#include "lists/refresh_rate.h"

namespace lists {

PhysicsState onRateChange(PhysicsState s, float oldHz, float newHz,
                          VsyncDeadline vsync) {
  // State carries no per-rate time base: offset and velocity are
  // rate-independent, and the frame step lives in the injected VsyncDeadline
  // consumed by the next integrate call. Preservation is therefore exact —
  // return the state untouched. Touch the parameters so -Werror builds stay
  // clean without changing behavior.
  (void)oldHz;
  (void)newHz;
  (void)vsync;
  return s;
}

}  // namespace lists

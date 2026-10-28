// Ring-buffered texture pool accounting (FR-GRAPHICS-008).
// Pure logic, no VMA calls.
#include "engine/graphics/textures/texture_pool.h"

#include <climits>

PoolOutcome poolAcquire(const PoolState& state, const TextureRequest& req) {
  PoolOutcome out;
  out.admitted = false;
  out.evicted = false;
  out.evictedBytes = 0;
  if (req.bytes <= 0 || state.capacityBytes <= 0 || state.ringSlots <= 0 ||
      state.usedBytes < 0 || state.usedSlots < 0) {
    out.status = Status::kInvalidArg;
    return out;
  }
  if (state.usedSlots >= state.ringSlots) {
    out.status = Status::kOverBudget;
    return out;
  }
  if (req.bytes > state.capacityBytes) {
    out.status = Status::kOverBudget;
    return out;
  }
  // Overflow-safe pressure path: 64-bit so INT_MAX-scale inputs cannot wrap
  // to admit-without-eviction. Fail closed when the true total is not
  // int-representable (no logic change for normal magnitudes).
  const long long total = (long long)state.usedBytes + (long long)req.bytes;
  const long long shortfall = total - (long long)state.capacityBytes;
  if (shortfall <= 0) {
    out.admitted = true;
    out.status = Status::kOk;
    return out;
  }
  if (total > INT_MAX || shortfall > INT_MAX) {
    out.status = Status::kOverBudget;
    return out;
  }
  out.admitted = true;
  out.evicted = true;
  out.evictedBytes = (int)shortfall;
  out.status = Status::kOk;
  return out;
}

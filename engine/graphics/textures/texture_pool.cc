// Ring-buffered texture pool accounting (FR-GRAPHICS-008).
// Pure logic, no VMA calls.
#include "engine/graphics/textures/texture_pool.h"

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
  const int shortfall = state.usedBytes + req.bytes - state.capacityBytes;
  if (shortfall <= 0) {
    out.admitted = true;
    out.status = Status::kOk;
    return out;
  }
  out.admitted = true;
  out.evicted = true;
  out.evictedBytes = shortfall;
  out.status = Status::kOk;
  return out;
}

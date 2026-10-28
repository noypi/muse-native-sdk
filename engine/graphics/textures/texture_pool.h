// Ring-buffered texture pool lifetime/eviction accounting (FR-GRAPHICS-008).
// Pure accounting model, no VMA/Vulkan calls: the caller passes pool usage and
// the request size; this unit only decides admit/evict. Eviction rules are
// identical for compressed and uncompressed textures (format-independent).
// Visible textures are never chosen for eviction: evictedBytes accounts only
// non-visible bytes, so evictedBytes <= prior usedBytes always holds.
#ifndef ENGINE_GRAPHICS_TEXTURES_TEXTURE_POOL_H_
#define ENGINE_GRAPHICS_TEXTURES_TEXTURE_POOL_H_

#include "engine/graphics/textures/texture_format.h"

struct TextureRequest {
  GpuFormat format = GpuFormat::kUncompressed;
  int bytes = 0;
  bool visible = false;
};

struct PoolState {
  int capacityBytes = 0;
  int usedBytes = 0;
  int ringSlots = 0;
  int usedSlots = 0;
};

enum class Status { kOk, kOverBudget, kInvalidArg };

struct PoolOutcome {
  bool admitted = false;
  bool evicted = false;
  int evictedBytes = 0;
  Status status = Status::kOk;
};

// Decides admission for one texture request against current pool usage.
// Invalid sizes (req.bytes <= 0, capacityBytes <= 0, ringSlots <= 0,
// usedBytes < 0, usedSlots < 0) -> kInvalidArg, admitted=false, no eviction.
// Full ring (usedSlots >= ringSlots) -> kOverBudget, admitted=false.
// Request larger than total capacity -> kOverBudget, admitted=false.
// Fits (usedBytes + bytes <= capacityBytes) -> kOk, admitted, no eviction.
// Pressure -> kOk, admitted, evicts exactly the shortfall
// (usedBytes + bytes - capacityBytes) counted in non-visible bytes.
// Format and visibility never change the path; visible requests are admitted
// under pressure with eviction charged to non-visible usage only.
PoolOutcome poolAcquire(const PoolState& state, const TextureRequest& req);

#endif  // ENGINE_GRAPHICS_TEXTURES_TEXTURE_POOL_H_

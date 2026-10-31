#include "pviews/hybrid_embedding.h"

// Hidden-window hybrid embedding pool with export + reclaim.
//
// Validation order mirrors src/z_order.cpp: id/geometry first, then the
// pool slot, then the export-capability gate. Every denial is fail-closed
// with the `{false, -1, false, false, false, <error>}` shape and no throw.
HybridResult attachHybridView(HybridPool& pool, SdkViewId id, ClipRect clip,
                              Transform /*t*/,
                              ExportCapability cap) noexcept {
  if (id.value == 0) {
    return HybridResult{false, -1, false, false, false,
                        HybridError::InvalidGeometry};
  }
  if (clip.right <= clip.left || clip.bottom <= clip.top) {
    return HybridResult{false, -1, false, false, false,
                        HybridError::InvalidGeometry};
  }
  if (clip.cornerRadius < 0) {
    return HybridResult{false, -1, false, false, false,
                        HybridError::InvalidGeometry};
  }
  for (const std::uint64_t existing : pool.ids) {
    if (existing == id.value) {
      int layer = 0;
      for (std::size_t i = 0; i < pool.ids.size(); ++i) {
        if (pool.ids[i] == id.value) {
          layer = static_cast<int>(i);
          break;
        }
      }
      if (cap == ExportCapability::NoExport) {
        return HybridResult{false, -1, false, false, false,
                            HybridError::ExportUnsupported};
      }
      return HybridResult{true, layer, true, true, true, HybridError::None};
    }
  }
  if (pool.ids.size() >= kHybridPoolCapacity) {
    return HybridResult{false, -1, false, false, false,
                        HybridError::PoolExhausted};
  }
  if (cap == ExportCapability::NoExport) {
    return HybridResult{false, -1, false, false, false,
                        HybridError::ExportUnsupported};
  }
  const int layer = static_cast<int>(pool.ids.size());
  pool.ids.push_back(id.value);
  return HybridResult{true, layer, true, true, true, HybridError::None};
}

void reclaimAll(HybridPool& pool) noexcept { pool.ids.clear(); }

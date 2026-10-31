#pragma once

#include <cstdint>
#include <vector>

#include "pviews/z_order.h"

struct SdkViewId {
  std::uint64_t value = 0;
};

enum class ExportCapability {
  ExportCapable,
  NoExport,
};

enum class HybridError {
  None,
  InvalidGeometry,
  PoolExhausted,
  ExportUnsupported,
};

struct HybridResult {
  bool placed = false;
  int layer = -1;
  bool clipApplied = false;
  bool pooled = false;
  bool exported = false;
  HybridError error = HybridError::None;
};

// Fixed hosting-window pool cap; tuning is out of scope (Q-PVIEWS-002).
// Fail-closed denial with PoolExhausted once full.
inline constexpr std::size_t kHybridPoolCapacity = 4;

struct HybridPool {
  std::vector<std::uint64_t> ids;
};

// Attaches the hidden host window for `id` inside layout bounds `clip`
// under transform `t`, exporting it for graphics DAG import when
// `cap == ExportCapable`. Never throws.
HybridResult attachHybridView(HybridPool& pool, SdkViewId id, ClipRect clip,
                              Transform t, ExportCapability cap) noexcept;

// Releases every pooled slot (lifecycle stop); idempotent. Never throws.
void reclaimAll(HybridPool& pool) noexcept;

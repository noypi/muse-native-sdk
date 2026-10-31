#ifndef ENGINE_GRAPHICS_MEMORY_VRAM_BUDGET_H_
#define ENGINE_GRAPHICS_MEMORY_VRAM_BUDGET_H_

// Adaptive VRAM budget + onTrimMemory flush tiers (NFR-GRAPHICS-004,
// AC-GRAPHICS-008). Hermetic: stdlib only, no wall clock, no I/O.
//
// The profile byte bound is injected through the VramPool constructor (it
// stands in for the DeviceFingerprint.ram_gb / Memory Budget Manager bound
// query: 32-48MB on <4GB low-end devices, 64-80MB on 4-6GB mid devices,
// 128MB+ on >=6GB high devices). The visible-texture set is identical
// before and after every trim tier: eviction never touches visible entries.

#include <cstddef>
#include <cstdint>
#include <map>

namespace vram_budget {

enum class TrimLevel { kRunningCritical, kRunningLow, kBackground };

enum class Status { kOk, kBudgetExceeded };

struct FlushResult {
  std::size_t freedBytes;
  std::size_t retainedVisibleCount;
};

class VramPool {
 public:
  explicit VramPool(std::size_t boundBytes);

  // Inserts (or replaces) texture `id`. Fail-closed: when the allocation
  // would push usage past the profile bound, returns kBudgetExceeded with
  // the pool unchanged.
  Status allocate(uint32_t id, std::size_t bytes, bool visible);

  std::size_t boundBytes() const;
  std::size_t usedBytes() const;
  std::size_t visibleBytes() const;
  std::size_t visibleCount() const;
  bool prefetchEnabled() const;

 private:
  friend FlushResult applyTrim(TrimLevel level, VramPool& pool);

  struct Entry {
    std::size_t bytes;
    bool visible;
  };

  std::size_t bound_bytes_;
  // std::map keeps eviction order deterministic across tiers.
  std::map<uint32_t, Entry> entries_;
  bool prefetch_enabled_ = true;
};

FlushResult applyTrim(TrimLevel level, VramPool& pool);

}  // namespace vram_budget

#endif  // ENGINE_GRAPHICS_MEMORY_VRAM_BUDGET_H_

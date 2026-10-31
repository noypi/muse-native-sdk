// Adaptive VRAM budget + onTrimMemory flush tiers (NFR-GRAPHICS-004,
// AC-GRAPHICS-008). See vram_budget.h for the contract.
#include "engine/graphics/memory/vram_budget.h"

#include <algorithm>

namespace vram_budget {

VramPool::VramPool(std::size_t boundBytes) : bound_bytes_(boundBytes) {}

Status VramPool::allocate(uint32_t id, std::size_t bytes, bool visible) {
  auto existing = entries_.find(id);
  std::size_t old_bytes = 0;
  if (existing != entries_.end()) {
    old_bytes = existing->second.bytes;
  }
  // Fail closed: never let usage exceed the profile bound; on rejection the
  // pool is unchanged (a replaced entry is restored below).
  if (usedBytes() - old_bytes + bytes > bound_bytes_) {
    return Status::kBudgetExceeded;
  }
  entries_[id] = Entry{bytes, visible};
  return Status::kOk;
}

std::size_t VramPool::boundBytes() const { return bound_bytes_; }

std::size_t VramPool::usedBytes() const {
  std::size_t total = 0;
  for (const auto& kv : entries_) {
    total += kv.second.bytes;
  }
  return total;
}

std::size_t VramPool::visibleBytes() const {
  std::size_t total = 0;
  for (const auto& kv : entries_) {
    if (kv.second.visible) {
      total += kv.second.bytes;
    }
  }
  return total;
}

std::size_t VramPool::visibleCount() const {
  std::size_t count = 0;
  for (const auto& kv : entries_) {
    if (kv.second.visible) {
      ++count;
    }
  }
  return count;
}

bool VramPool::prefetchEnabled() const { return prefetch_enabled_; }

FlushResult applyTrim(TrimLevel level, VramPool& pool) {
  const std::size_t used_before = pool.usedBytes();
  const std::size_t visible_before = pool.visibleCount();

  std::size_t target_free = 0;
  switch (level) {
    case TrimLevel::kRunningCritical:
      // Immediate 50% flush; stop feeding the cache under pressure.
      target_free = used_before / 2u;
      pool.prefetch_enabled_ = false;
      break;
    case TrimLevel::kRunningLow:
      // 25% flush + disable prefetch.
      target_free = used_before / 4u;
      pool.prefetch_enabled_ = false;
      break;
    case TrimLevel::kBackground:
      // Full flush retaining only visible textures; prefetch state is left
      // to the caller's resume path (unspecified by the contract).
      target_free = used_before - pool.visibleBytes();
      break;
  }

  // Evict invisible entries first (whole entries, then a partial shrink of
  // the last one if the fraction lands mid-entry). Visible entries are
  // never touched, so the visible set is identical before/after.
  std::size_t freed = 0;
  for (auto it = pool.entries_.begin();
       freed < target_free && it != pool.entries_.end();) {
    if (it->second.visible) {
      ++it;
      continue;
    }
    const std::size_t take =
        std::min(it->second.bytes, target_free - freed);
    it->second.bytes -= take;
    freed += take;
    if (it->second.bytes == 0) {
      it = pool.entries_.erase(it);
    } else {
      ++it;
    }
  }

  FlushResult result;
  result.freedBytes = used_before - pool.usedBytes();
  result.retainedVisibleCount = visible_before;
  return result;
}

}  // namespace vram_budget

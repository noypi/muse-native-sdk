#include "core/mem/leak_hooks.h"

#if CORE_MEM_LEAK_HOOKS

namespace core {
namespace mem {

void LeakHooks::record(void* p, uint32_t subsys, uint32_t phase) noexcept {
  if (p == nullptr) return;
  try {
    for (Entry& e : entries_) {
      if (e.p == p) {
        e.subsys = subsys;
        e.phase = phase;
        return;
      }
    }
    entries_.push_back(Entry{p, subsys, phase});
  } catch (...) {
    // Attribution is best-effort; never throw from the hook.
  }
}

void LeakHooks::release(void* p) noexcept {
  if (p == nullptr) return;
  for (size_t i = 0; i < entries_.size(); ++i) {
    if (entries_[i].p == p) {
      entries_[i] = entries_.back();
      entries_.pop_back();
      return;
    }
  }
}

size_t LeakHooks::survivors(Entry* out, size_t n) const noexcept {
  if (out == nullptr || n == 0) return 0;
  size_t count = entries_.size() < n ? entries_.size() : n;
  for (size_t i = 0; i < count; ++i) {
    out[i] = entries_[i];
  }
  return count;
}

}  // namespace mem
}  // namespace core

#endif

// Arena implementation (NFR-CORE-002, AC-CORE-003).
//
// The backing store lives for the Arena lifetime; per-frame allocation
// only moves the cursor defined in arena.h. Overflow reports
// CORE_ALLOC_HEAP_ESCAPE in instrumented builds and returns nullptr.

#include "arena.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace muse {
namespace core {

namespace {

void ReportHeapEscape(size_t n) {
#if defined(MUSE_INSTRUMENTED_ALLOC) || !defined(NDEBUG)
  std::fprintf(stderr, "CORE_ALLOC_HEAP_ESCAPE: arena request for %u bytes denied\n",
               static_cast<unsigned>(n));
#else
  (void)n;
#endif
}

}  // namespace

Arena::Arena(size_t capacity) noexcept : base_(nullptr), capacity_(capacity), offset_(0) {
  if (capacity_ == 0) {
    return;
  }
  base_ = static_cast<unsigned char*>(std::malloc(capacity_));
  if (base_ == nullptr) {
    capacity_ = 0;
  }
}

Arena::~Arena() noexcept {
  std::free(base_);
  base_ = nullptr;
}

void* Arena::allocate(size_t n, size_t align) noexcept {
  if (base_ == nullptr || n == 0) {
    if (n != 0) {
      ReportHeapEscape(n);
    }
    return nullptr;
  }
  if (align == 0) {
    align = 1;
  }
  // The mask trick below is only correct for powers of two: reject any
  // other alignment instead of returning a misaligned pointer.
  if ((align & (align - 1)) != 0) {
    ReportHeapEscape(n);
    return nullptr;
  }
  const uintptr_t base = reinterpret_cast<uintptr_t>(base_);
  uintptr_t cursor = base + static_cast<uintptr_t>(offset_);
  const uintptr_t mask = static_cast<uintptr_t>(align) - 1;
  // Align is a power of two on every supported render-path TU (enforced
  // above).
  const uintptr_t aligned = (cursor + mask) & ~mask;
  const size_t padding = static_cast<size_t>(aligned - base) - offset_;
  if (padding > capacity_ - offset_ || n > capacity_ - offset_ - padding) {
    ReportHeapEscape(n);
    return nullptr;
  }
  offset_ += padding + n;
  return reinterpret_cast<void*>(aligned);
}

void Arena::reset() noexcept { offset_ = 0; }

}  // namespace core
}  // namespace muse

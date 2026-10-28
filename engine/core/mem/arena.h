// Per-frame monotonic arena for the render path (NFR-CORE-002, AC-CORE-003).
//
// The backing store is reserved once at construction with the given
// capacity. allocate() bumps a cursor inside that store; reset() rewinds
// the cursor in O(1) at frame end. The store is fixed for the frame:
// a request that does not fit returns nullptr and reports
// CORE_ALLOC_HEAP_ESCAPE in instrumented builds instead of falling back
// to the global heap.
#pragma once

#include <cstddef>

namespace muse {
namespace core {

using std::size_t;

class Arena {
 public:
  explicit Arena(size_t capacity) noexcept;
  ~Arena() noexcept;

  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;

  // Bump-allocate n bytes with the given alignment from the store.
  // Returns nullptr (and reports CORE_ALLOC_HEAP_ESCAPE where
  // instrumented) when the request does not fit.
  // Precondition: align is a power of two (0 is treated as 1). A
  // non-power-of-two alignment is rejected with nullptr + escape report
  // instead of returning a misaligned pointer.
  void* allocate(size_t n, size_t align) noexcept;

  // Rewind the cursor so the whole store is usable for the next frame.
  void reset() noexcept;

  size_t capacity() const noexcept { return capacity_; }
  size_t used() const noexcept { return offset_; }

 private:
  unsigned char* base_;
  size_t capacity_;
  size_t offset_;
};

}  // namespace core
}  // namespace muse

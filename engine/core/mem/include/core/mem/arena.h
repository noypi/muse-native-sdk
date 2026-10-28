#pragma once

#include <cstddef>

namespace core {
namespace mem {

using std::size_t;

// Per-frame bump allocator over a caller-owned fixed buffer.
//
// The buffer is fixed for the frame: allocate() bumps a cursor inside it
// and reset() rewinds the cursor in O(1). A request that does not fit
// returns nullptr and never grows or touches the global heap.
class FrameArena {
 public:
  FrameArena(void* buf, size_t cap) noexcept;

  FrameArena(const FrameArena&) = delete;
  FrameArena& operator=(const FrameArena&) = delete;

  // Bump-allocate n bytes with the given alignment. Returns nullptr when
  // the request does not fit. A zero align is treated as 1; a non-power-
  // of-two alignment is rejected with nullptr.
  void* allocate(size_t n, size_t align) noexcept;

  // Rewind the cursor so the whole buffer is reusable (O(1)).
  void reset() noexcept;

  size_t used() const noexcept { return offset_; }
  size_t capacity() const noexcept { return cap_; }

 private:
  unsigned char* base_;
  size_t cap_;
  size_t offset_;
};

// Test-build trip wire: while armed, any global-heap allocation latches
// globalHeapUsed() until the guard exits its scope.
//
// WARNING: the global operator new/delete replacements live in
// engine/core/mem/src/arena.cpp and replace the PROCESS-WIDE heap
// operators for every translation unit linked with core_mem. Production
// render code MUST NOT link that TU: the replacement is scoped to test
// builds only (link the trip-wire TU from test targets, keep it out of
// production link lines). Linking it into production risks ODR clashes
// with other operator-new definitions, cross-TU new/delete mismatch, and
// heap-alloc misattribution outside the guard.
class RenderGuard {
 public:
  explicit RenderGuard(bool on) noexcept;
  ~RenderGuard() noexcept;

  RenderGuard(const RenderGuard&) = delete;
  RenderGuard& operator=(const RenderGuard&) = delete;

  static bool globalHeapUsed() noexcept;

 private:
  bool on_;
  bool prev_armed_;
  bool prev_tripped_;
};

namespace detail {
// Called by the global operator new replacements in arena.cpp.
void NoteHeapAlloc() noexcept;
}  // namespace detail

}  // namespace mem
}  // namespace core

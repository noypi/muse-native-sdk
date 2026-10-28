#include "core/mem/arena.h"

#include <cstdint>
#include <cstdlib>
#include <new>

namespace core {
namespace mem {

namespace {

// Thread-local trip wire state. Armed only while a RenderGuard(true) is
// live on this thread; latched on any global-heap allocation attempt.
thread_local bool g_armed = false;
thread_local bool g_tripped = false;

bool IsPowerOfTwo(size_t v) { return v != 0 && (v & (v - 1)) == 0; }

}  // namespace

FrameArena::FrameArena(void* buf, size_t cap) noexcept
    : base_(static_cast<unsigned char*>(buf)), cap_(buf != nullptr ? cap : 0), offset_(0) {}

void* FrameArena::allocate(size_t n, size_t align) noexcept {
  if (base_ == nullptr) {
    return nullptr;
  }
  if (align == 0) {
    align = 1;
  }
  if (!IsPowerOfTwo(align)) {
    return nullptr;
  }
  uintptr_t cur = reinterpret_cast<uintptr_t>(base_ + offset_);
  uintptr_t aligned = (cur + static_cast<uintptr_t>(align - 1)) &
                      ~static_cast<uintptr_t>(align - 1);
  uintptr_t base = reinterpret_cast<uintptr_t>(base_);
  size_t pad = static_cast<size_t>(aligned - base) - offset_;
  if (offset_ > cap_ || pad > cap_ - offset_) {
    return nullptr;
  }
  size_t avail = cap_ - offset_ - pad;
  if (n > avail) {
    return nullptr;
  }
  offset_ += pad + n;
  return reinterpret_cast<void*>(aligned);
}

void FrameArena::reset() noexcept { offset_ = 0; }

RenderGuard::RenderGuard(bool on) noexcept
    : on_(on), prev_armed_(g_armed), prev_tripped_(g_tripped) {
  if (on_) {
    if (!prev_armed_) {
      // Outermost guard on this thread: arm and start with a clear latch.
      g_armed = true;
      g_tripped = false;
    } else {
      // Nested guard while already armed: keep the outer latch so a trip
      // recorded before entry (or by an outer scope) is not discarded.
      g_armed = true;
    }
  }
}

RenderGuard::~RenderGuard() noexcept {
  if (on_) {
    bool inner_tripped = g_tripped;
    g_armed = prev_armed_;
    if (prev_armed_) {
      // Nest-safe exit: OR the inner trip into the outer latch instead of
      // overwriting it, so a heap alloc inside the inner guard stays
      // visible to the outer guard.
      g_tripped = prev_tripped_ || inner_tripped;
    } else {
      g_tripped = prev_tripped_;
    }
  }
}

bool RenderGuard::globalHeapUsed() noexcept { return g_armed && g_tripped; }

namespace detail {

void NoteHeapAlloc() noexcept {
  if (g_armed) {
    g_tripped = true;
  }
}

}  // namespace detail

}  // namespace mem
}  // namespace core

// Global replacement operators: latch the trip wire, then serve from the
// C heap. FrameArena/Pool never pass through here (caller-owned storage).
//
// TEST-BUILD ONLY: these replacements are process-wide for every TU linked
// with this object. Production render code must not link this TU; test
// targets link it to arm the RenderGuard trip wire (see arena.h warning).
void* operator new(std::size_t n) {
  core::mem::detail::NoteHeapAlloc();
  if (void* p = std::malloc(n != 0 ? n : 1)) {
    return p;
  }
  throw std::bad_alloc();
}

void operator delete(void* p) noexcept { std::free(p); }

void* operator new[](std::size_t n) {
  core::mem::detail::NoteHeapAlloc();
  if (void* p = std::malloc(n != 0 ? n : 1)) {
    return p;
  }
  throw std::bad_alloc();
}

void operator delete[](void* p) noexcept { std::free(p); }

void operator delete(void* p, std::size_t) noexcept { std::free(p); }

void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
  core::mem::detail::NoteHeapAlloc();
  return std::malloc(n != 0 ? n : 1);
}

void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }

void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
  core::mem::detail::NoteHeapAlloc();
  return std::malloc(n != 0 ? n : 1);
}

void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

// C++17 aligned trip-wire overloads: aligned heap allocs must latch the
// guard exactly like plain new, otherwise over-aligned allocations would
// bypass RenderGuard unnoticed.
void* operator new(std::size_t n, std::align_val_t a) {
  core::mem::detail::NoteHeapAlloc();
  void* p = nullptr;
  const std::size_t align = static_cast<std::size_t>(a);
  if (::posix_memalign(&p, align != 0 ? align : alignof(void*),
                       n != 0 ? n : 1) != 0) {
    throw std::bad_alloc();
  }
  return p;
}

void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }

void* operator new[](std::size_t n, std::align_val_t a) {
  return ::operator new(n, a);
}

void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }

void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
  std::free(p);
}

void operator delete[](void* p, std::size_t, std::align_val_t) noexcept {
  std::free(p);
}

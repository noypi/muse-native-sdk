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
    g_armed = true;
    g_tripped = false;
  }
}

RenderGuard::~RenderGuard() noexcept {
  if (on_) {
    g_armed = prev_armed_;
    g_tripped = prev_tripped_;
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

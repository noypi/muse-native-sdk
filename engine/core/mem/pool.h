// Fixed-capacity pool for hot per-frame objects (NFR-CORE-002, AC-CORE-003).
//
// Pre-sized Pool<T,N> keeps N slots inline and hands them out via
// acquire(); release() returns a slot to the free list. There is no
// growth on the frame path: acquire() returns nullptr and reports
// CORE_ALLOC_HEAP_ESCAPE in instrumented builds when the pool is empty.
#pragma once

#include <cstddef>
#include <cstdio>

namespace muse {
namespace core {

using std::size_t;

namespace detail {

// Pool-exhaustion escape report, mirroring Arena's CORE_ALLOC_HEAP_ESCAPE
// shape: emitted in instrumented builds (MUSE_INSTRUMENTED_ALLOC) and in
// debug builds, silent otherwise.
inline void ReportPoolExhaustion() {
#if defined(MUSE_INSTRUMENTED_ALLOC) || !defined(NDEBUG)
  std::fprintf(stderr,
               "CORE_ALLOC_HEAP_ESCAPE: pool acquire denied (empty)\n");
#endif
}

}  // namespace detail

// Fixed-capacity pool for hot per-frame objects (NFR-CORE-002, AC-CORE-003).
//
// POD-only with caller-managed lifetime: slots are raw uninitialized
// storage. acquire() does NOT run placement-new and release() does NOT run
// the destructor — the caller constructs/destroys T in the returned
// storage. T must therefore be trivially constructible/destructible in
// practice (plain data). release() must be given exactly a pointer
// previously returned by acquire() that has not been released yet;
// passing a foreign pointer or releasing twice corrupts the free list
// (undefined behavior) and is never diagnosed.
template <typename T, size_t N>
class Pool {
 public:
  Pool() noexcept : head_(nullptr), freeCount_(N) {
    for (size_t i = 0; i < N; ++i) {
      slots_[i].next = head_;
      head_ = &slots_[i];
    }
  }

  Pool(const Pool&) = delete;
  Pool& operator=(const Pool&) = delete;

  // Take one slot from the pool, or nullptr (reporting
  // CORE_ALLOC_HEAP_ESCAPE where instrumented) when empty.
  T* acquire() noexcept {
    if (head_ == nullptr) {
      detail::ReportPoolExhaustion();
      return nullptr;
    }
    Slot* slot = head_;
    head_ = slot->next;
    --freeCount_;
    return reinterpret_cast<T*>(&slot->storage);
  }

  // Return a previously acquired slot to the pool.
  void release(T* ptr) noexcept {
    if (ptr == nullptr) {
      return;
    }
    Slot* slot = reinterpret_cast<Slot*>(static_cast<void*>(ptr));
    slot->next = head_;
    head_ = slot;
    ++freeCount_;
  }

  size_t capacity() const noexcept { return N; }
  size_t freeCount() const noexcept { return freeCount_; }

 private:
  struct Slot {
    Slot* next;
    alignas(T) unsigned char storage[sizeof(T)];
  };

  Slot slots_[N];
  Slot* head_;
  size_t freeCount_;
};

}  // namespace core
}  // namespace muse

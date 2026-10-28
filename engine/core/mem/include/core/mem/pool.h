#pragma once

#include <cstddef>

namespace core {
namespace mem {

using std::size_t;

// Fixed-capacity slab pool for hot per-frame objects (draw items, nodes,
// tokens). Slots live inline; acquire() hands one out and release()
// returns it. There is no growth: acquire() returns nullptr without any
// heap fallback when the pool is empty.
//
// Slots are raw uninitialized storage: acquire() does not construct T and
// release() does not destroy it; the caller manages T's lifetime in the
// returned storage. Passing a foreign pointer or releasing twice is
// undefined behavior.
template <typename T, size_t N>
class Pool {
 public:
  Pool() noexcept : head_(nullptr), free_count_(N) {
    for (size_t i = 0; i < N; ++i) {
      slots_[i].next = head_;
      head_ = &slots_[i];
    }
  }

  Pool(const Pool&) = delete;
  Pool& operator=(const Pool&) = delete;

  // Take one slot, or nullptr when the pool is empty (no heap fallback).
  T* acquire() noexcept {
    if (head_ == nullptr) {
      return nullptr;
    }
    Slot* slot = head_;
    head_ = slot->next;
    --free_count_;
    return reinterpret_cast<T*>(&slot->storage);
  }

  // Return a previously acquired slot.
  void release(T* ptr) noexcept {
    if (ptr == nullptr) {
      return;
    }
    Slot* slot = reinterpret_cast<Slot*>(static_cast<void*>(ptr));
    slot->next = head_;
    head_ = slot;
    ++free_count_;
  }

  size_t capacity() const noexcept { return N; }
  size_t freeCount() const noexcept { return free_count_; }

 private:
  // Union so the free-list link shares bytes with the slot storage and
  // every slot address converts back to its Slot losslessly, even when
  // sizeof(T) < sizeof(void*).
  struct Slot {
    union {
      Slot* next;
      alignas(T) unsigned char storage[sizeof(T)];
    };
  };

  Slot slots_[N];
  Slot* head_;
  size_t free_count_;
};

}  // namespace mem
}  // namespace core

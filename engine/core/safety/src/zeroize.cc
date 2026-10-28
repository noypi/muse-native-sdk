// Secure zeroization implementation. See zeroize.h for the contract.

#include "core/safety/zeroize.h"

namespace {

// Volatile sink: writing through a volatile-qualified lvalue is an
// observable side effect, so the compiler may not elide or reorder
// the stores away. The indirection through a volatile function
// pointer additionally defeats whole-program dead-store elimination
// at -O2 and above.
using WipeFn = void (*)(volatile unsigned char*, std::size_t) noexcept;

void wipe_loop(volatile unsigned char* ptr, std::size_t len) noexcept {
  for (std::size_t i = 0; i < len; ++i) {
    ptr[i] = 0;
  }
}

}  // namespace

void muse_core_safety_zeroize(void* ptr, std::size_t len) noexcept {
  if (len == 0) {
    return;
  }
  if (ptr == nullptr) {
    // Tolerated misuse: null with nonzero length performs no write
    // and does not trap.
    return;
  }
  auto* p = static_cast<volatile unsigned char*>(ptr);
  // Opaque the call so the stores cannot be proven dead.
  static volatile WipeFn wipe_fn = &wipe_loop;
  wipe_fn(p, len);
#if defined(__GNUC__) || defined(__clang__)
  // Compiler barrier: wiped bytes are observable to later code.
  __asm__ volatile("" ::: "memory");
#endif
}

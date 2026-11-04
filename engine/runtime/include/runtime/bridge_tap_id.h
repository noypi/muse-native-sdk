// Shared bridge tapId counter (BUG-M3-NATIVE-006 T1 / FR-M3-013).
// Single per-process monotonic identity minted at UP-resolve; JNI carries
// only this integer (never a raw pointer). Header-only: one shared instance
// across translation units via the function-local static below (exactly one
// object program-wide for an inline function). Rotation in [1, INT32_MAX],
// never 0/-1. Consume-only: drag/CANCEL/unmount paths never mint.

#pragma once

#include <atomic>
#include <cstdint>

namespace runtime {

// Mint the next bridge tapId. Lock-free CAS loop; wraps INT32_MAX -> 1 so
// the minted value always stays in [1, INT32_MAX] (0 and -1 impossible).
inline int MintTapId() noexcept {
  static std::atomic<std::uint32_t> s_counter{1};
  constexpr std::uint32_t kMax =
      static_cast<std::uint32_t>(INT32_MAX);
  std::uint32_t cur = s_counter.load(std::memory_order_relaxed);
  for (;;) {
    const std::uint32_t next = (cur >= kMax || cur == 0) ? 1u : cur + 1u;
    if (s_counter.compare_exchange_weak(cur, next,
                                        std::memory_order_relaxed,
                                        std::memory_order_relaxed)) {
      return static_cast<int>(cur);
    }
  }
}

}  // namespace runtime

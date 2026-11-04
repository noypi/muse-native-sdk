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

namespace bridge_tap_detail {
// Single shared per-process tapId sequence behind MintTapId and its
// observe-only views below. Function-local statics: exactly one object
// program-wide for an inline function.
inline std::atomic<std::uint32_t>& ForwardCounter() noexcept {
  static std::atomic<std::uint32_t> s_counter{1};
  return s_counter;
}
// Last id handed out by MintTapId; -1 before the first mint (fail-closed —
// 0 and -1 both lie outside the minted [1, INT32_MAX] range, so -1 can never
// be mistaken for a live ticket).
inline std::atomic<int>& LastMintedSlot() noexcept {
  static std::atomic<int> s_last{-1};
  return s_last;
}
}  // namespace bridge_tap_detail

// Mint the next bridge tapId. Lock-free CAS loop; wraps INT32_MAX -> 1 so
// the minted value always stays in [1, INT32_MAX] (0 and -1 impossible).
inline int MintTapId() noexcept {
  std::atomic<std::uint32_t>& s_counter =
      bridge_tap_detail::ForwardCounter();
  constexpr std::uint32_t kMax =
      static_cast<std::uint32_t>(INT32_MAX);
  std::uint32_t cur = s_counter.load(std::memory_order_relaxed);
  for (;;) {
    const std::uint32_t next = (cur >= kMax || cur == 0) ? 1u : cur + 1u;
    if (s_counter.compare_exchange_weak(cur, next,
                                        std::memory_order_relaxed,
                                        std::memory_order_relaxed)) {
      bridge_tap_detail::LastMintedSlot().store(static_cast<int>(cur),
                                                std::memory_order_relaxed);
      return static_cast<int>(cur);
    }
  }
}

// Observe-only view: the last id handed out by MintTapId, or -1 before the
// first mint. Never mints, never advances the sequence.
inline int LastMintedTapId() noexcept {
  return bridge_tap_detail::LastMintedSlot().load(std::memory_order_relaxed);
}

// Observe-only view of the shared forward counter. Loads only — the sequence
// advances exclusively through MintTapId.
inline const std::atomic<std::uint32_t>& TapIdCounter() noexcept {
  return bridge_tap_detail::ForwardCounter();
}

}  // namespace runtime

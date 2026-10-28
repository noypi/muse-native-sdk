#pragma once

#include <atomic>
#include <cstddef>

namespace core {
namespace mem {

// Allocation-instrumented soak counter (NFR-CORE-009 / AC-CORE-022).
//
// Counts global-heap allocations observed on the render path. Render-path
// allocation sites call noteRenderAlloc() with the source polarity:
//   fromHeap=false => arena/pool-sourced (not counted)
//   fromHeap=true  => global-heap-sourced (counted)
// Header-only; counter is a process-wide atomic so soak accounting works
// without linking an extra TU.
class Instrument {
 public:
  static void reset() noexcept { counter_.store(0, std::memory_order_relaxed); }
  static size_t renderHeapAllocs() noexcept {
    return counter_.load(std::memory_order_relaxed);
  }
  static void noteRenderAlloc(bool fromHeap) noexcept {
    if (fromHeap) counter_.fetch_add(1, std::memory_order_relaxed);
  }

 private:
  inline static std::atomic<size_t> counter_{0};
};

}  // namespace mem
}  // namespace core

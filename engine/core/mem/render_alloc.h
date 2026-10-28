// Render-path heap ban (NFR-CORE-002, AC-CORE-003).
//
// Types that live on the render path derive from RenderAlloc so that any
// use of the global heap inside a frame fails at compile time. The class
// deletes every global-heap entry point; the instrumented build additionally
// reports CORE_ALLOC_HEAP_ESCAPE if heap use is observed on the frame path.
#pragma once

#include <cstddef>

namespace muse {
namespace core {

using std::size_t;

struct RenderAlloc {
  RenderAlloc() noexcept = default;
  ~RenderAlloc() noexcept = default;

  // Global-heap entry points are banned on the render path: every such
  // call inside a frame fails the instrumented build.
  static void* operator new(size_t) = delete;
  static void* operator new[](size_t) = delete;
  static void operator delete(void*) = delete;
  static void operator delete[](void*) = delete;
};

}  // namespace core
}  // namespace muse

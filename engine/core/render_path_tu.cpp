// Render-path translation unit (NFR-CORE-006, AC-CORE-008).
//
// This TU compiles with -fno-exceptions (see
// cmake/PluginVisibility.cmake), so it must contain no escaping
// exception machinery. The frame entry point below is noexcept: any
// invariant failure on the render boundary routes to
// renderPathViolation, which aborts with the CORE_EXCEPTION_ESCAPE
// diagnostic instead of propagating.
#include "exception_policy.h"

#include <cstdio>
#include <cstdlib>

namespace muse {
namespace core {

[[noreturn]] void renderPathViolation(const char* reason) noexcept {
  std::fputs("CORE_EXCEPTION_ESCAPE: ", stderr);
  std::fputs(reason != nullptr ? reason : "render boundary failure", stderr);
  std::fputs("\n", stderr);
  std::fflush(stderr);
  std::abort();
}

// Frame entry point for the render path. noexcept by contract: a failed
// precondition cannot escape and instead ends in renderPathViolation.
void runRenderFrame(bool frameValid) noexcept {
  if (!frameValid) {
    renderPathViolation("CORE_EXCEPTION_ESCAPE: invalid frame state");
  }
}

}  // namespace core
}  // namespace muse

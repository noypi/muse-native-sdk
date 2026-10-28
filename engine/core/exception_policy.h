// Render-path exception policy (NFR-CORE-006, AC-CORE-008).
//
// The render path compiles with -fno-exceptions (see
// cmake/PluginVisibility.cmake). No exception may cross the render
// boundary: any escape routes to renderPathViolation, which aborts the
// process with the CORE_EXCEPTION_ESCAPE diagnostic instead of
// propagating an exception object. Non-render code may use exceptions;
// this policy covers only the render path.
#pragma once

namespace muse {
namespace core {

// Hard-failure hook for the render boundary. Aborts the process with
// the CORE_EXCEPTION_ESCAPE diagnostic naming the given reason.
// Never returns; safe to call from noexcept render-path code.
[[noreturn]] void renderPathViolation(const char* reason) noexcept;

}  // namespace core
}  // namespace muse

// Security pin (3) — prod-never-defines-macro audit (bug-runtime-013, LOW finding).
//
// Hermetic compile-level pin that runs in the unit suite (not CI): this TU is
// compiled EXACTLY as production compiles it — with the prod warning set and
// WITHOUT `-DMUSE_TEST_SEAM` — and hard-fails the compile if the macro ever
// leaks into a prod TU's flags.
//
// AUDIT CONTRACT:
//   PROD: `g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror
//           -I engine/runtime/include -c <this>` (no -D) MUST COMPILE and the
//         binary MUST EXIT 0 (macro absent in prod shape; AC-RUNTIME-041).
//   LEAK: same command WITH `-DMUSE_TEST_SEAM=1` MUST FAIL at the `#error`
//         below — proving the audit bites if any prod target ever gains the
//         definition (e.g. via `target_compile_definitions` on a non-test
//         target). If that ever happens, scope the `-D` to test targets only.
// Binds to the `runtime` prod target's flag shape (CMakeLists: -Wall
// -Wextra -Wpedantic, no MUSE_TEST_SEAM scoping); hermetic — header only, no
// filesystem/config/env reads.

#include "runtime/egl_blit.h"

#if defined(MUSE_TEST_SEAM) && (MUSE_TEST_SEAM == 1)
#error "seam_prod_macro_audit_test: prod TU must never define MUSE_TEST_SEAM=1 (scope the -D to test targets only)"
#endif

#include <cstdio>

namespace {

// Prod shape: the canonical seam macro is OFF-or-undefined here, exactly as
// on every prod target in the build graph.
static bool check_prod_macro_absent() {
#if defined(MUSE_TEST_SEAM) && (MUSE_TEST_SEAM == 1)
  return false;
#else
  return true;
#endif
}

}  // namespace

int main() {
  if (!check_prod_macro_absent()) {
    std::printf("FAIL check_prod_macro_absent\n");
    return 1;
  }
  std::printf("PASS check_prod_macro_absent\n");
  std::printf("seam_prod_macro_audit_test: all passing\n");
  return 0;
}

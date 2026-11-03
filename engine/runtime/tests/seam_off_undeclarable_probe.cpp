// Security pin (1) — OFF-undeclarable probe (bug-runtime-013, security review 2026-11-03).
//
// COMPILE CONTRACT (gate presence, not just behavior):
//   OFF: `g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror
//          -I engine/runtime/include -c engine/runtime/tests/seam_off_undeclarable_probe.cpp`
//        (no -DMUSE_TEST_SEAM) MUST FAIL — `egl_blit_test::FaultHooks` /
//        `SetFaultHooks` are undeclared without the macro (AC-RUNTIME-040
//        limb a / AC-RUNTIME-041).
//   ON:  same command with `-DMUSE_TEST_SEAM=1` MUST COMPILE — the seam is
//        nameable from test TUs only.
// This file intentionally names the seam UNCONDITIONALLY (no SEAM_ON guard):
// it must never be added to a prod target or an OFF suite; its pass criterion
// is the OFF compile failure recorded in unit-tests/T1.md.

#include "runtime/egl_blit.h"

// Unconditional seam use — resolves only when the gate leaks or ON is set.
static void touch_seam() {
  egl_blit_test::FaultHooks hooks;
  hooks.fail_next_swap = true;
  egl_blit_test::SetFaultHooks(&hooks);
  egl_blit_test::SetFaultHooks(nullptr);
}

int main() {
  touch_seam();
  return 0;
}

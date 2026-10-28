// T2 contract tests — NFR-CORE-009 / AC-CORE-021 (TC-CORE-021).
// Leak hooks attribute surviving allocations to (subsystem, phase) and
// are zero-overhead when disabled (CORE_MEM_LEAK_HOOKS=0).
//
// Binds ONLY to the T2 `Interface:` line; never to implementation.
// Hermetic: C++17 + planned header <core/mem/leak_hooks.h> only; no
// network, no env/config reads, no filesystem, no live services.
//
// Expected state: RED — <core/mem/leak_hooks.h> does not exist yet, so
// this file fails to compile until the coder implements T2.
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "core/mem/leak_hooks.h"

// --- tiny harness (matches engine/runtime/tests convention) ----------------
static int s_failures = 0;

#define CHECK(name, expr)                                     \
  do {                                                        \
    if (!(expr)) {                                            \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, name); \
      ++s_failures;                                           \
    } else {                                                  \
      std::printf("PASS %s\n", name);                         \
    }                                                         \
  } while (0)

#if !defined(CORE_MEM_LEAK_HOOKS)
#define CORE_MEM_LEAK_HOOKS 1
#endif

// --- cases ----------------------------------------------------------------

#if CORE_MEM_LEAK_HOOKS

// Happy path: a deliberately leaked allocation is attributed to the exact
// (subsystem, phase) pair and reported by survivors().
static void check_leak_attributed_to_subsystem_phase() {
  core::mem::LeakHooks hooks;
  int token_a = 0;
  int token_b = 0;
  hooks.record(&token_a, /*subsys=*/3, /*phase=*/7);
  hooks.record(&token_b, /*subsys=*/5, /*phase=*/9);
  hooks.release(&token_b);  // b is freed; a deliberately leaks
  core::mem::LeakHooks::Entry out[4];
  size_t n = hooks.survivors(out, 4);
  CHECK("exactly one survivor", n == 1);
  if (n == 1) {
    CHECK("survivor pointer matches", out[0].p == &token_a);
    CHECK("survivor subsystem matches", out[0].subsys == 3);
    CHECK("survivor phase matches", out[0].phase == 7);
  }
}

// release() removes the entry; a fully released set has no survivors.
static void check_release_clears_survivor() {
  core::mem::LeakHooks hooks;
  int token = 0;
  hooks.record(&token, 1, 2);
  hooks.release(&token);
  core::mem::LeakHooks::Entry out[2];
  CHECK("no survivors after release", hooks.survivors(out, 2) == 0);
}

// Boundary: output buffer smaller than the survivor set truncates safely.
static void check_survivors_truncates_to_output_size() {
  core::mem::LeakHooks hooks;
  int t0 = 0;
  int t1 = 0;
  int t2 = 0;
  hooks.record(&t0, 1, 1);
  hooks.record(&t1, 2, 2);
  hooks.record(&t2, 3, 3);
  core::mem::LeakHooks::Entry out[2];
  size_t n = hooks.survivors(out, 2);
  CHECK("survivors capped at output size", n == 2);
  hooks.release(&t0);
  hooks.release(&t1);
  hooks.release(&t2);
  CHECK("all released afterwards", hooks.survivors(out, 2) == 0);
}

// Error path: releasing an unknown pointer is a safe no-op.
static void check_release_unknown_pointer_safe() {
  core::mem::LeakHooks hooks;
  int known = 0;
  int unknown = 0;
  hooks.record(&known, 4, 4);
  hooks.release(&unknown);  // never recorded: must not crash or disturb
  core::mem::LeakHooks::Entry out[2];
  size_t n = hooks.survivors(out, 2);
  CHECK("known survivor undisturbed", n == 1 && out[0].p == &known);
  hooks.release(&known);
}

// Error path: null output buffer / zero capacity never writes.
static void check_survivors_zero_capacity_safe() {
  core::mem::LeakHooks hooks;
  int token = 0;
  hooks.record(&token, 8, 8);
  size_t n = hooks.survivors(nullptr, 0);
  CHECK("zero-capacity survivors returns 0", n == 0);
  hooks.release(&token);
}

// Distinct (subsys, phase) pairs are attributed independently.
static void check_distinct_pairs_attributed_independently() {
  core::mem::LeakHooks hooks;
  int t0 = 0;
  int t1 = 0;
  hooks.record(&t0, 10, 20);
  hooks.record(&t1, 10, 21);
  core::mem::LeakHooks::Entry out[4];
  size_t n = hooks.survivors(out, 4);
  CHECK("both survivors reported", n == 2);
  hooks.release(&t0);
  hooks.release(&t1);
}

#else  // CORE_MEM_LEAK_HOOKS == 0: compiled-out path

// Zero-overhead contract: calls inline to no-ops and survivors() == 0.
static void check_hooks_compiled_out() {
  core::mem::LeakHooks hooks;
  int token = 0;
  hooks.record(&token, 3, 7);  // must be a no-op
  hooks.release(&token);       // must be a no-op
  core::mem::LeakHooks::Entry out[2];
  CHECK("survivors returns 0 when disabled", hooks.survivors(out, 2) == 0);
  // Object carries no state when disabled.
  CHECK("disabled hooks object is empty", sizeof(hooks) == 1);
}

#endif

int main() {
#if CORE_MEM_LEAK_HOOKS
  check_leak_attributed_to_subsystem_phase();
  check_release_clears_survivor();
  check_survivors_truncates_to_output_size();
  check_release_unknown_pointer_safe();
  check_survivors_zero_capacity_safe();
  check_distinct_pairs_attributed_independently();
#else
  check_hooks_compiled_out();
#endif
  if (s_failures == 0) {
    std::printf("ALL GREEN leak_hooks_test\n");
    return 0;
  }
  std::printf("%d FAILURES leak_hooks_test\n", s_failures);
  return 1;
}

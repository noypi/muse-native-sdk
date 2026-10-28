// T4 contract tests — NFR-CORE-007 (no ACs in source; honest coverage of
// the Rationale/Notes prose only) + reference NFR-PERFORMANCE-003 soak
// thresholds (self-comparative fallback per plan A5).
//
// Binds ONLY to the T4 `Interface:` line; never to implementation.
// Hermetic: C++17 + planned header <core/budget/manager.h> only; no
// network, no env/config reads, no filesystem, no live services.
// Numeric tier bounds (32-48 MiB, 128+ MiB, 4/6 GiB) come from the
// NFR-CORE-007 Rationale prose, not from invented ACs.
//
// Expected state: RED — <core/budget/manager.h> does not exist yet, so
// this file fails to compile until the coder implements T4.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

#include "core/budget/manager.h"

// --- tiny harness (matches engine/runtime/tests convention) ----------------
static int s_failures = 0;

#define CHECK(name, expr)                                       \
  do {                                                          \
    if (!(expr)) {                                              \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, name); \
      ++s_failures;                                             \
    } else {                                                    \
      std::printf("PASS %s\n", name);                           \
    }                                                           \
  } while (0)

static const size_t kMiB = 1024u * 1024u;
static const size_t kGiB = 1024u * 1024u * 1024u;

// --- cases ----------------------------------------------------------------

// Rationale: Low-End (RAM below 4 GiB) => VRAM cap within [32, 48] MiB.
static void check_tier_low_end_cap() {
  core::budget::Manager::Config low =
      core::budget::Manager::tierForRam(2 * kGiB);
  CHECK("low-end cap >= 32 MiB", low.vramCapBytes >= 32 * kMiB);
  CHECK("low-end cap <= 48 MiB", low.vramCapBytes <= 48 * kMiB);
  CHECK("low-end ram echoed", low.ramBytes == 2 * kGiB);

  core::budget::Manager::Config edge =
      core::budget::Manager::tierForRam(4 * kGiB - 1);
  CHECK("just-under-4GiB still low tier (cap <= 48 MiB)",
        edge.vramCapBytes <= 48 * kMiB);
}

// Rationale: Mid/High-End (RAM at least 6 GiB) => VRAM cap >= 128 MiB.
static void check_tier_high_end_cap() {
  core::budget::Manager::Config high =
      core::budget::Manager::tierForRam(8 * kGiB);
  CHECK("high-end cap >= 128 MiB", high.vramCapBytes >= 128 * kMiB);
  CHECK("high-end ram echoed", high.ramBytes == 8 * kGiB);

  core::budget::Manager::Config edge =
      core::budget::Manager::tierForRam(6 * kGiB);
  CHECK("exactly-6GiB is high tier (cap >= 128 MiB)",
        edge.vramCapBytes >= 128 * kMiB);
}

// Between 4-6 GiB the cap interpolates document order: monotone and
// bounded by the two documented tiers (header documents the curve).
static void check_tier_mid_band_monotone() {
  core::budget::Manager::Config lo =
      core::budget::Manager::tierForRam(4 * kGiB);
  core::budget::Manager::Config mid =
      core::budget::Manager::tierForRam(5 * kGiB);
  core::budget::Manager::Config hi =
      core::budget::Manager::tierForRam(6 * kGiB);
  CHECK("mid band bounded below by low tier",
        mid.vramCapBytes >= 32 * kMiB);
  CHECK("mid band monotone non-decreasing (4G <= 5G)",
        mid.vramCapBytes >= lo.vramCapBytes);
  CHECK("mid band monotone non-decreasing (5G <= 6G)",
        hi.vramCapBytes >= mid.vramCapBytes);
}

// acquire within cap succeeds; over-cap fails without changing usage.
// Usage is observed behaviorally (a fill probe), never via internals.
static void check_acquire_release_within_cap() {
  core::budget::Manager::Config cfg =
      core::budget::Manager::tierForRam(8 * kGiB);
  core::budget::Manager mgr(cfg);
  CHECK("acquire 1 MiB succeeds", mgr.acquireVram(1 * kMiB));
  CHECK("acquire rest-of-cap-minus-1 succeeds",
        mgr.acquireVram(cfg.vramCapBytes - 1 * kMiB));
  CHECK("over-cap acquire fails", !mgr.acquireVram(1 * kMiB));
  mgr.releaseVram(cfg.vramCapBytes - 1 * kMiB);
  CHECK("space freed by release is reusable",
        mgr.acquireVram(1 * kMiB));
  mgr.releaseVram(1 * kMiB);
  mgr.releaseVram(1 * kMiB);
}

// Oversized / zero-size requests are rejected safely, never corrupting
// the budget.
static void check_acquire_invalid_sizes() {
  core::budget::Manager::Config cfg =
      core::budget::Manager::tierForRam(2 * kGiB);
  core::budget::Manager mgr(cfg);
  CHECK("zero-size acquire rejected", !mgr.acquireVram(0));
  CHECK("huge acquire rejected",
        !mgr.acquireVram(cfg.vramCapBytes + kGiB));
  CHECK("budget still fully usable after rejections",
        mgr.acquireVram(cfg.vramCapBytes));
  mgr.releaseVram(cfg.vramCapBytes);
}

// onTrim with a critical level evicts: previously-full usage drops so a
// new acquire that failed before the trim succeeds afterwards.
static void check_on_trim_critical_evicts() {
  core::budget::Manager::Config cfg =
      core::budget::Manager::tierForRam(2 * kGiB);
  core::budget::Manager mgr(cfg);
  CHECK("fill to cap", mgr.acquireVram(cfg.vramCapBytes));
  CHECK("full: extra acquire fails", !mgr.acquireVram(1 * kMiB));
  mgr.onTrim(80);  // critical ComponentCallbacks2-class level
  CHECK("post-trim: acquire succeeds (usage dropped)",
        mgr.acquireVram(1 * kMiB));
  mgr.releaseVram(1 * kMiB);
}

// onTrim with a low (non-critical) level keeps budget usable and never
// corrupts accounting.
static void check_on_trim_low_level_safe() {
  core::budget::Manager::Config cfg =
      core::budget::Manager::tierForRam(8 * kGiB);
  core::budget::Manager mgr(cfg);
  CHECK("acquire 1 MiB", mgr.acquireVram(1 * kMiB));
  mgr.onTrim(5);  // non-critical level
  CHECK("budget usable after low trim",
        mgr.acquireVram(1 * kMiB));
  mgr.releaseVram(1 * kMiB);
  mgr.releaseVram(1 * kMiB);
}

// RAII guard: holds budget in scope (peer acquire fails), releases on
// scope exit (peer acquire succeeds).
static void check_vram_guard_releases_on_scope_exit() {
  core::budget::Manager::Config cfg =
      core::budget::Manager::tierForRam(2 * kGiB);
  core::budget::Manager mgr(cfg);
  {
    core::budget::VramGuard guard(mgr, cfg.vramCapBytes);
    CHECK("guard holds full cap: peer acquire fails",
          !mgr.acquireVram(1 * kMiB));
  }
  CHECK("guard released: peer acquire succeeds",
        mgr.acquireVram(1 * kMiB));
  mgr.releaseVram(1 * kMiB);
}

// RAII guard releases even on the exception path.
static void check_vram_guard_releases_on_exception() {
  core::budget::Manager::Config cfg =
      core::budget::Manager::tierForRam(2 * kGiB);
  core::budget::Manager mgr(cfg);
  try {
    core::budget::VramGuard guard(mgr, 4 * kMiB);
    CHECK("guard holds: peer over-remaining fails",
          !mgr.acquireVram(cfg.vramCapBytes));
    throw std::runtime_error("navigation cancelled");
  } catch (const std::runtime_error&) {
    // Guard destructor must have run during unwind.
  }
  CHECK("post-unwind: full cap reusable",
        mgr.acquireVram(cfg.vramCapBytes));
  mgr.releaseVram(cfg.vramCapBytes);
}

// size_t-wraparound guard: used_ + bytes must not wrap past the cap check.
// acquireVram(SIZE_MAX) with 1 MiB already used would wrap `used_ + bytes`
// under a naive comparison; it must be rejected and leave accounting intact.
static void check_acquire_vram_wraparound_rejected() {
  core::budget::Manager::Config cfg =
      core::budget::Manager::tierForRam(8 * kGiB);
  core::budget::Manager mgr(cfg);
  CHECK("acquire 1 MiB", mgr.acquireVram(1 * kMiB));
  CHECK("SIZE_MAX acquire rejected (no wraparound bypass)",
        !mgr.acquireVram(SIZE_MAX));
  CHECK("huge acquire past remaining cap rejected",
        !mgr.acquireVram(cfg.vramCapBytes));
  CHECK("budget intact after wraparound attempts",
        mgr.acquireVram(cfg.vramCapBytes - 1 * kMiB));
  mgr.releaseVram(cfg.vramCapBytes - 1 * kMiB);
  mgr.releaseVram(1 * kMiB);
  CHECK("budget fully reusable after wraparound attempts",
        mgr.acquireVram(cfg.vramCapBytes));
  mgr.releaseVram(cfg.vramCapBytes);
}

int main() {
  check_tier_low_end_cap();
  check_tier_high_end_cap();
  check_tier_mid_band_monotone();
  check_acquire_release_within_cap();
  check_acquire_invalid_sizes();
  check_acquire_vram_wraparound_rejected();
  check_on_trim_critical_evicts();
  check_on_trim_low_level_safe();
  check_vram_guard_releases_on_scope_exit();
  check_vram_guard_releases_on_exception();
  if (s_failures == 0) {
    std::printf("ALL GREEN budget_tiers_test\n");
    return 0;
  }
  std::printf("%d FAILURES budget_tiers_test\n", s_failures);
  return 1;
}

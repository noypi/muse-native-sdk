// T6 contract tests — FR-MOTION-014 / AC-047 (TC-MOTION-047).
// Hermetic: C++17 + committed motion headers only; no network, no env/
// config reads, no filesystem, no live services. The a11y reduced-motion
// setting arrives as the boolean input to motion_setReducedMotion; no OS
// accessibility API is read. Time is driven only via nowMs parameters.
// Binds to the T6 `Interface:` line (reproduced verbatim in T6.md).
//
// ASSUMPTION (variant naming): with reduced motion enabled, every pattern
// reports a variant string that is non-null, non-empty, and different from
// the full pattern name; fade-through (already a fade) reports "cross-fade".
// The exact per-pattern table (shorten vs cross-fade) is the coder's choice
// as long as slide/scale motion is replaced and feedback stays perceptible.
// ASSUMPTION (toggle scope): motion_setReducedMotion is process-global and
// takes effect on the next activeVariant call without restart; each case
// below sets the mode it needs explicitly so cases stay independent.
// ASSUMPTION (PatternId reuse): PatternId values come from T4 patternNamed;
// reduced_motion.h refers to the same motion::PatternId (T6 depends on T4).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>

#include "motion/patterns.h"
#include "motion/reduced_motion.h"

namespace {

const char* kNames[5] = {
    "fade-through",
    "shared-axis-x",
    "shared-axis-y",
    "shared-axis-z",
    "container-transform",
};

bool streq(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) return false;
  return std::strcmp(a, b) == 0;
}

}  // namespace

// Named case: default (off) — activeVariant returns the full pattern name.
static bool check_default_off_reports_full_pattern_names() {
  motion::motion_setReducedMotion(false);
  try {
    for (const char* n : kNames) {
      motion::PatternId p = motion::patternNamed(n);
      const char* v = motion::activeVariant(p);
      if (!streq(v, n)) return false;
    }
  } catch (const std::exception&) {
    return false;
  }
  return true;
}

// Named case: T6 Interface — with reduced motion off, motion_lastBudgetShed
// is false on the nominal path (no silent shedding).
static bool check_budget_not_shed_on_nominal_path_when_off() {
  motion::motion_setReducedMotion(false);
  try {
    motion::PatternId p = motion::patternNamed("fade-through");
    motion::runEnter(p, 2001, 1000);
    (void)motion::patternActive(p, 2001);
  } catch (const std::exception&) {
    return false;
  }
  return !motion::motion_lastBudgetShed();
}

// Named case: TC-MOTION-047 — with reduced motion on, every pattern reports
// its defined variant (slide/scale replaced, never a silent drop: variant
// is non-null and non-empty).
static bool check_enabled_reports_defined_variant_per_pattern() {
  motion::motion_setReducedMotion(true);
  bool ok = true;
  try {
    for (const char* n : kNames) {
      motion::PatternId p = motion::patternNamed(n);
      const char* v = motion::activeVariant(p);
      if (v == nullptr || v[0] == '\0') {
        ok = false;
        break;
      }
      if (streq(v, n)) {
        ok = false;  // full-motion name while reduced => no substitution
        break;
      }
    }
  } catch (const std::exception&) {
    ok = false;
  }
  motion::motion_setReducedMotion(false);  // restore default for other cases
  return ok;
}

// Named case: fade-through degrades to cross-fade when enabled (T6 Interface
// e.g. clause).
static bool check_fade_through_variant_is_cross_fade_when_enabled() {
  motion::motion_setReducedMotion(true);
  bool ok = false;
  try {
    motion::PatternId p = motion::patternNamed("fade-through");
    ok = streq(motion::activeVariant(p), "cross-fade");
  } catch (const std::exception&) {
    ok = false;
  }
  motion::motion_setReducedMotion(false);
  return ok;
}

// Named case: with reduced motion on, the variant still renders — runEnter
// drives the target (essential state-change feedback stays perceptible,
// never a silent drop per plan A4).
static bool check_enabled_variant_still_renders_enter() {
  motion::motion_setReducedMotion(true);
  bool ok = false;
  try {
    motion::PatternId p = motion::patternNamed("shared-axis-x");
    motion::runEnter(p, 2002, 5000);
    ok = motion::patternActive(p, 2002);
  } catch (const std::exception&) {
    ok = false;
  }
  motion::motion_setReducedMotion(false);
  return ok;
}

// Named case: with reduced motion on, motion_lastBudgetShed stays false on
// the nominal path (variant substitution is declared, not silent shedding).
static bool check_budget_not_shed_on_nominal_path_when_enabled() {
  motion::motion_setReducedMotion(true);
  bool ok = false;
  try {
    motion::PatternId p = motion::patternNamed("container-transform");
    motion::runEnter(p, 2003, 9000);
    (void)motion::patternActive(p, 2003);
    ok = !motion::motion_lastBudgetShed();
  } catch (const std::exception&) {
    ok = false;
  }
  motion::motion_setReducedMotion(false);
  return ok;
}

// Named case: runtime toggle takes effect without restart — on then off
// restores the full pattern name on the very next call.
static bool check_toggle_takes_effect_without_restart() {
  try {
    motion::PatternId p = motion::patternNamed("shared-axis-z");
    motion::motion_setReducedMotion(true);
    const char* reduced = motion::activeVariant(p);
    if (reduced == nullptr || streq(reduced, "shared-axis-z")) {
      motion::motion_setReducedMotion(false);
      return false;
    }
    motion::motion_setReducedMotion(false);
    return streq(motion::activeVariant(p), "shared-axis-z");
  } catch (const std::exception&) {
    motion::motion_setReducedMotion(false);
    return false;
  }
}

// Named case: exit path honors the toggle too — reduced variant active on
// runExit while enabled.
static bool check_exit_renders_under_reduced_motion() {
  motion::motion_setReducedMotion(true);
  bool ok = false;
  try {
    motion::PatternId p = motion::patternNamed("shared-axis-y");
    motion::runExit(p, 2004, 12000);
    ok = motion::patternActive(p, 2004);
  } catch (const std::exception&) {
    ok = false;
  }
  motion::motion_setReducedMotion(false);
  return ok;
}

#define RUN_CASE(name)                             \
  do {                                             \
    if (!(name)()) {                               \
      std::printf("FAIL %s\n", #name);             \
      ++failures;                                  \
    } else {                                       \
      std::printf("PASS %s\n", #name);             \
    }                                              \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_default_off_reports_full_pattern_names);
  RUN_CASE(check_budget_not_shed_on_nominal_path_when_off);
  RUN_CASE(check_enabled_reports_defined_variant_per_pattern);
  RUN_CASE(check_fade_through_variant_is_cross_fade_when_enabled);
  RUN_CASE(check_enabled_variant_still_renders_enter);
  RUN_CASE(check_budget_not_shed_on_nominal_path_when_enabled);
  RUN_CASE(check_toggle_takes_effect_without_restart);
  RUN_CASE(check_exit_renders_under_reduced_motion);
  if (failures > 0) {
    std::printf("reduced_motion_test: %d failing\n", failures);
    return 1;
  }
  std::printf("reduced_motion_test: all passing\n");
  return 0;
}

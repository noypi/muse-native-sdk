// T2 contract tests — FR-MOTION-002 / AC-006 (TC-MOTION-006).
// Hermetic: C++17 + committed motion headers only; no network, no env/
// config reads, no filesystem, no live services. Virtual clock is injected
// through the nowMs parameters; the OS clock is never touched.
// Binds to the T2 `Interface:` lines (reproduced verbatim in the T2 contract).
//
// ASSUMPTION (shared clock/solver headers): per plan A5/Risks the clock and
// spring solver live in coder-owned shared headers (proposed names
// `motion/clock.h`, `motion/solver.h`). These cases never include them
// directly; they drive time only via nowMs and assert interpolation +
// retarget continuity, so the coder may name/shape those headers freely.
// ASSUMPTION (initial value): a registered (id, property) that has never
// transitioned samples to 0.0f, so the first onPropertyChange animates
// 0.0f -> toValue. If the coder seeds a different rest value, update this
// file's expectations via reconciliation (expectations, not call sites).
// ASSUMPTION (unregistered sample): sampling an (id, property) pair that was
// never registered returns 0.0f without throwing. Sampling a null property
// name returns 0.0f without crashing.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>

#include "motion/implicit.h"

namespace {

bool approx(float a, float b, float eps = 1e-3f) {
  return std::fabs(a - b) <= eps;
}

bool mentions(const std::exception& e, const char* needle) {
  return std::strstr(e.what(), needle) != nullptr;
}

const char* kOpacityToken = "effects-default";

}  // namespace

// Named case: registering a property with an unknown token throws TokenError
// naming the token (T2 Interface: markImplicit throws on unknown token).
static bool check_mark_implicit_unknown_token_throws_naming_token() {
  const char* missing = "motion.nope.missing-token";
  try {
    motion::markImplicit(1, "opacity", missing);
  } catch (const std::exception& e) {
    return mentions(e, missing);
  }
  return false;
}

// Named case: null token name throws instead of crashing.
static bool check_mark_implicit_null_token_throws() {
  try {
    motion::markImplicit(2, "opacity", nullptr);
  } catch (const std::exception&) {
    return true;
  }
  return false;
}

// Named case: TC-MOTION-006 happy path — register opacity, change it, step
// the virtual clock; samples interpolate monotonically from old to new with
// no imperative per-frame code.
static bool check_change_interpolates_from_old_to_new() {
  const motion::NodeId id = 11;
  try {
    motion::markImplicit(id, "opacity", kOpacityToken);
  } catch (const std::exception&) {
    return false;
  }
  motion::onPropertyChange(id, "opacity", 1.0f, 1000);
  float at_start = motion::sample(id, "opacity", 1000);
  float at_mid = motion::sample(id, "opacity", 1000 + 150);
  float at_end = motion::sample(id, "opacity", 1000 + 100000);
  if (!approx(at_start, 0.0f, 0.05f)) return false;
  if (!(at_mid > at_start && at_mid < 1.0f)) return false;
  return approx(at_end, 1.0f, 0.02f);
}

// Named case: sampling before the start time clamps to the start value.
static bool check_sample_before_start_clamps() {
  const motion::NodeId id = 12;
  try {
    motion::markImplicit(id, "opacity", kOpacityToken);
  } catch (const std::exception&) {
    return false;
  }
  motion::onPropertyChange(id, "opacity", 1.0f, 5000);
  float before = motion::sample(id, "opacity", 4000);
  return approx(before, 0.0f, 0.05f);
}

// Named case: no-op when value unchanged — repeating the current target
// does not restart the transition (T2 Interface).
static bool check_unchanged_value_is_noop() {
  const motion::NodeId id = 13;
  try {
    motion::markImplicit(id, "opacity", kOpacityToken);
  } catch (const std::exception&) {
    return false;
  }
  motion::onPropertyChange(id, "opacity", 1.0f, 8000);
  float settled = motion::sample(id, "opacity", 8000 + 100000);
  if (!approx(settled, 1.0f, 0.02f)) return false;
  motion::onPropertyChange(id, "opacity", 1.0f, 8000 + 100);
  float after = motion::sample(id, "opacity", 8000 + 100);
  return approx(after, settled, 0.02f);
}

// Named case: second change mid-flight retargets continuously — no snap back
// to the old value, converges to the newest target.
static bool check_midflight_change_retargets_continuously() {
  const motion::NodeId id = 14;
  try {
    motion::markImplicit(id, "opacity", kOpacityToken);
  } catch (const std::exception&) {
    return false;
  }
  motion::onPropertyChange(id, "opacity", 1.0f, 20000);
  float mid = motion::sample(id, "opacity", 20000 + 120);
  motion::onPropertyChange(id, "opacity", 0.0f, 20000 + 120);
  float at_retarget = motion::sample(id, "opacity", 20000 + 120);
  if (!approx(at_retarget, mid, 0.05f)) return false;
  float later = motion::sample(id, "opacity", 20000 + 120 + 100000);
  return approx(later, 0.0f, 0.05f);
}

// Named case: per-property isolation — animating opacity does not move an
// independently registered transform property on the same node.
static bool check_properties_are_independent_per_node() {
  const motion::NodeId id = 15;
  try {
    motion::markImplicit(id, "opacity", kOpacityToken);
    motion::markImplicit(id, "transform", "spatial-default");
  } catch (const std::exception&) {
    return false;
  }
  motion::onPropertyChange(id, "opacity", 1.0f, 30000);
  float transform = motion::sample(id, "transform", 30000 + 150);
  return approx(transform, 0.0f, 0.05f);
}

// Named case: per-node isolation — the same property on two nodes animates
// independently.
static bool check_nodes_are_independent() {
  try {
    motion::markImplicit(16, "opacity", kOpacityToken);
    motion::markImplicit(17, "opacity", kOpacityToken);
  } catch (const std::exception&) {
    return false;
  }
  motion::onPropertyChange(16, "opacity", 1.0f, 40000);
  float other = motion::sample(17, "opacity", 40000 + 150);
  return approx(other, 0.0f, 0.05f);
}

// Named case: boundary — sampling an unregistered pair returns rest without
// throwing; null property name does not crash.
static bool check_unregistered_sample_returns_rest() {
  float a;
  try {
    a = motion::sample(9999, "opacity-never-registered", 50000);
  } catch (const std::exception&) {
    return false;
  }
  if (!approx(a, 0.0f, 1e-6f)) return false;
  try {
    float b = motion::sample(9999, nullptr, 50000);
    (void)b;
  } catch (const std::exception&) {
    return true;  // throwing on null is also acceptable; must not crash.
  }
  return true;
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
  RUN_CASE(check_mark_implicit_unknown_token_throws_naming_token);
  RUN_CASE(check_mark_implicit_null_token_throws);
  RUN_CASE(check_change_interpolates_from_old_to_new);
  RUN_CASE(check_sample_before_start_clamps);
  RUN_CASE(check_unchanged_value_is_noop);
  RUN_CASE(check_midflight_change_retargets_continuously);
  RUN_CASE(check_properties_are_independent_per_node);
  RUN_CASE(check_nodes_are_independent);
  RUN_CASE(check_unregistered_sample_returns_rest);
  if (failures > 0) {
    std::printf("implicit_test: %d failing\n", failures);
    return 1;
  }
  std::printf("implicit_test: all passing\n");
  return 0;
}

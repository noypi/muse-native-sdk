// T1 contract tests — FR-MOTION-013 / AC-MOTION-042, AC-MOTION-043
// (TC-MOTION-042/043/044 surface; enables AC-MOTION-045).
// Hermetic: C++17 + committed motion headers only; no network, no env/
// config reads, no filesystem, no live services, no clock needed (pure
// table lookup, fully deterministic).
// Binds to the T1 `Interface:` line; token names, enum/struct spellings
// and the version-constant name below are the test's proposal because the
// plan Detail leaves the table contents to the coder (plan A1 greenfield).
// See the T1 contract reconciliation rule: if the coder names them
// differently, update these call sites, not the expectations.
// QA: this file uses only C++17, printf-based RUN_CASE style copied from
// engine/runtime/tests/layout_test.cpp, and asserts on std::exception
// what() text so a TokenError member rename cannot break the build.
//
// ASSUMPTION (token names): minimal version-pinned table covering the
// dimensions the T4 patterns need — durations short1..long4, easings
// standard/emphasized/accelerate/decelerate, one spatial spring and one
// effects spring. Names are proposed as "<dim>-<variant>" below.
// ASSUMPTION (pinned values): the spec pins values per Q-MOTION-001 but
// states no numbers, so these cases assert kind, positivity, ordering
// (short < long) and repeat determinism rather than exact M3 numbers.
// Exact numeric pins are coder-owned; any numeric drift is a versioned
// change per AC-MOTION-044, not a test failure.
// ASSUMPTION (TokenError shape): plan `TokenError{Name, Reason}` — cases
// assert the thrown message names the token (and property where
// applicable) via what(); member access is avoided deliberately.
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#include "motion/tokens.h"

namespace {

// what() of the caught error must mention `needle`.
bool mentions(const std::exception& e, const char* needle) {
  return std::strstr(e.what(), needle) != nullptr;
}

bool same_token(const motion::MotionToken& a, const motion::MotionToken& b) {
  return a.kind == b.kind && a.durationMs == b.durationMs &&
         ((a.easing == nullptr && b.easing == nullptr) ||
          (a.easing != nullptr && b.easing != nullptr &&
           std::strcmp(a.easing, b.easing) == 0)) &&
         a.stiffness == b.stiffness && a.damping == b.damping;
}

}  // namespace

// Named case: a known duration token resolves to its pinned values and
// repeat lookups agree exactly (AC-MOTION-042, AC-MOTION-044).
static bool check_known_duration_token_resolves_to_pinned_values() {
  motion::MotionToken first;
  motion::MotionToken second;
  try {
    first = motion::resolveToken("duration-short1");
    second = motion::resolveToken("duration-short1");
  } catch (const std::exception&) {
    return false;
  }
  if (first.kind != motion::TokenKind::Duration) return false;
  if (first.durationMs <= 0) return false;
  return same_token(first, second);
}

// Named case: duration tokens order short before long (spec dimension
// short1 through long4); relations, not literals.
static bool check_duration_tokens_order_short_before_long() {
  motion::MotionToken short_tok;
  motion::MotionToken long_tok;
  try {
    short_tok = motion::resolveToken("duration-short1");
    long_tok = motion::resolveToken("duration-long2");
  } catch (const std::exception&) {
    return false;
  }
  if (short_tok.kind != motion::TokenKind::Duration) return false;
  if (long_tok.kind != motion::TokenKind::Duration) return false;
  return short_tok.durationMs < long_tok.durationMs;
}

// Named case: a known easing token resolves with a named curve
// (AC-MOTION-042: component references a token name, not a literal).
static bool check_known_easing_token_resolves() {
  motion::MotionToken tok;
  try {
    tok = motion::resolveToken("easing-emphasized");
  } catch (const std::exception&) {
    return false;
  }
  if (tok.kind != motion::TokenKind::Easing) return false;
  return tok.easing != nullptr && tok.easing[0] != '\0';
}

// Named case: a known spatial spring resolves with positive physics
// constants (AC-MOTION-042 spatial-spring dimension).
static bool check_known_spatial_spring_resolves() {
  motion::MotionToken tok;
  try {
    tok = motion::resolveToken("spatial-default");
  } catch (const std::exception&) {
    return false;
  }
  if (tok.kind != motion::TokenKind::SpatialSpring) return false;
  return tok.stiffness > 0.0f && tok.damping > 0.0f;
}

// Named case: a known effects spring resolves with positive physics
// constants (AC-MOTION-042 effects-spring dimension).
static bool check_known_effects_spring_resolves() {
  motion::MotionToken tok;
  try {
    tok = motion::resolveToken("effects-default");
  } catch (const std::exception&) {
    return false;
  }
  if (tok.kind != motion::TokenKind::EffectsSpring) return false;
  return tok.stiffness > 0.0f && tok.damping > 0.0f;
}

// Named case: unknown name throws naming the token (T1 acceptance).
static bool check_unknown_name_throws_naming_token() {
  const char* missing = "motion.nope.missing-token";
  try {
    motion::resolveToken(missing);
  } catch (const std::exception& e) {
    return mentions(e, missing);
  }
  return false;  // resolved an unknown token: AC-MOTION-042 forbids this.
}

// Named case: empty name throws rather than resolving a default.
static bool check_empty_name_throws() {
  try {
    motion::resolveToken("");
  } catch (const std::exception& e) {
    return mentions(e, "empty") || mentions(e, "unknown") ||
           mentions(e, "token") || e.what()[0] != '\0';
  }
  return false;
}

// Named case: null name throws instead of crashing (boundary).
static bool check_null_name_throws_without_crashing() {
  try {
    motion::resolveToken(nullptr);
  } catch (const std::exception&) {
    return true;
  }
  return false;
}

// Named case: effects spring driving geometry is rejected naming both the
// property and the token (AC-MOTION-043).
static bool check_effects_spring_for_geometry_throws() {
  const char* property = "position";
  try {
    motion::checkSpringClass(motion::SpringClass::Effects, property);
  } catch (const std::exception& e) {
    return mentions(e, property) &&
           (mentions(e, "effects") || mentions(e, "Effects") ||
            mentions(e, "spatial") || mentions(e, "Spatial"));
  }
  return false;  // class misuse silently accepted: AC-MOTION-043 forbids.
}

// Named case: the mirror misuse — spatial spring driving opacity — is
// rejected naming the property (AC-MOTION-043 "or vice versa").
static bool check_spatial_spring_for_opacity_throws() {
  const char* property = "opacity";
  try {
    motion::checkSpringClass(motion::SpringClass::Spatial, property);
  } catch (const std::exception& e) {
    return mentions(e, property);
  }
  return false;
}

// Named case: correct class pairings do not throw — spatial drives
// geometry, effects drives colour/opacity (AC-MOTION-043 happy path).
static bool check_correct_spring_classes_do_not_throw() {
  try {
    motion::checkSpringClass(motion::SpringClass::Spatial, "position");
    motion::checkSpringClass(motion::SpringClass::Spatial, "size");
    motion::checkSpringClass(motion::SpringClass::Spatial, "shape");
    motion::checkSpringClass(motion::SpringClass::Effects, "opacity");
    motion::checkSpringClass(motion::SpringClass::Effects, "color");
  } catch (const std::exception&) {
    return false;
  }
  return true;
}

// Named case: the token set carries a non-empty version pin
// (AC-MOTION-044: version-pinned, changes are versioned).
static bool check_token_scheme_version_is_pinned() {
  const char* v = motion::kTokenSchemeVersion;
  return v != nullptr && v[0] != '\0';
}

// Named case: resolution is deterministic across calls (pinning means the
// same name always yields the same values).
static bool check_resolution_is_deterministic() {
  motion::MotionToken a;
  motion::MotionToken b;
  try {
    a = motion::resolveToken("easing-standard");
    b = motion::resolveToken("easing-standard");
  } catch (const std::exception&) {
    return false;
  }
  return same_token(a, b);
}

#define RUN_CASE(name)                                   \
  do {                                                   \
    if (!(name)()) {                                     \
      std::printf("FAIL %s\n", #name);                   \
      ++failures;                                       \
    } else {                                             \
      std::printf("PASS %s\n", #name);                   \
    }                                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_known_duration_token_resolves_to_pinned_values);
  RUN_CASE(check_duration_tokens_order_short_before_long);
  RUN_CASE(check_known_easing_token_resolves);
  RUN_CASE(check_known_spatial_spring_resolves);
  RUN_CASE(check_known_effects_spring_resolves);
  RUN_CASE(check_unknown_name_throws_naming_token);
  RUN_CASE(check_empty_name_throws);
  RUN_CASE(check_null_name_throws_without_crashing);
  RUN_CASE(check_effects_spring_for_geometry_throws);
  RUN_CASE(check_spatial_spring_for_opacity_throws);
  RUN_CASE(check_correct_spring_classes_do_not_throw);
  RUN_CASE(check_token_scheme_version_is_pinned);
  RUN_CASE(check_resolution_is_deterministic);
  if (failures > 0) {
    std::printf("motion_tokens_test: %d failing\n", failures);
    return 1;
  }
  std::printf("motion_tokens_test: all passing\n");
  return 0;
}

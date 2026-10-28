// T2 contract tests — FR-MOTION-013 / AC-MOTION-043.
// Hermetic: C++17 + committed motion headers only; no network, no env/
// config reads, no filesystem, no live services. Deterministic: pure
// in-process checks, no wall clock, no randomness.
// Binds to the T2 `Interface:` line (reproduced verbatim in unit-tests/T2.md).
//
// ASSUMPTION (token spellings): the plan binds the two classes to T1's token
// maps but pins no literal token strings. The tests use the T1 key spellings
// ("position"/"size"/"shape" spatial; "colour"/"opacity" effects); if the
// coder pins different spellings, literals update via reconciliation.
// ASSUMPTION (namespace): the plan `Interface:` line is unqualified; the
// tests use the existing `motion::` convention (cf. solver.h).
// ASSUMPTION (SpringClass enumerators): the plan names
// `template <SpringClass C> struct TypedSpring;` without enumerator
// spellings; the tests use `motion::SpringClass::Spatial/Effects` and
// reconcile by rename if the coder differs.
// ASSUMPTION (null errorOut): tolerated, still returns the verdict.
#include <cstdio>
#include <string>
#include <type_traits>

#include "motion/spring_class.h"

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

// Spatial tokens accepted for geometry properties.
bool check_spatial_accepted_for_position() {
  std::string error = "untouched";
  bool ok = motion::motion_checkSpringClass(motion::AnimProperty::Position,
                                            "position", &error);
  return ok;
}

bool check_spatial_accepted_for_size() {
  std::string error;
  return motion::motion_checkSpringClass(motion::AnimProperty::Size, "size",
                                         &error);
}

bool check_spatial_accepted_for_shape() {
  std::string error;
  return motion::motion_checkSpringClass(motion::AnimProperty::Shape, "shape",
                                         &error);
}

// Effects tokens accepted for colour/opacity properties.
bool check_effects_accepted_for_colour() {
  std::string error;
  return motion::motion_checkSpringClass(motion::AnimProperty::Colour,
                                         "colour", &error);
}

bool check_effects_accepted_for_opacity() {
  std::string error;
  return motion::motion_checkSpringClass(motion::AnimProperty::Opacity,
                                         "opacity", &error);
}

// Spatial token rejected for effects properties; error names property+token.
bool check_spatial_rejected_for_colour() {
  std::string error;
  bool ok = motion::motion_checkSpringClass(motion::AnimProperty::Colour,
                                            "position", &error);
  if (ok) return false;
  return contains(error, "Colour") && contains(error, "position");
}

bool check_spatial_rejected_for_opacity() {
  std::string error;
  bool ok = motion::motion_checkSpringClass(motion::AnimProperty::Opacity,
                                            "size", &error);
  if (ok) return false;
  return contains(error, "Opacity") && contains(error, "size");
}

// Effects token rejected for geometry properties; error names property+token.
bool check_effects_rejected_for_position() {
  std::string error;
  bool ok = motion::motion_checkSpringClass(motion::AnimProperty::Position,
                                            "colour", &error);
  if (ok) return false;
  return contains(error, "Position") && contains(error, "colour");
}

bool check_effects_rejected_for_size() {
  std::string error;
  bool ok = motion::motion_checkSpringClass(motion::AnimProperty::Size,
                                            "opacity", &error);
  if (ok) return false;
  return contains(error, "Size") && contains(error, "opacity");
}

bool check_effects_rejected_for_shape() {
  std::string error;
  bool ok = motion::motion_checkSpringClass(motion::AnimProperty::Shape,
                                            "opacity", &error);
  if (ok) return false;
  return contains(error, "Shape") && contains(error, "opacity");
}

// Unknown token is rejected with a non-empty error (boundary).
bool check_unknown_token_rejected() {
  std::string error;
  bool ok = motion::motion_checkSpringClass(motion::AnimProperty::Position,
                                            "no-such-spring", &error);
  return !ok && !error.empty();
}

// Empty token is rejected with a non-empty error (boundary).
bool check_empty_token_rejected() {
  std::string error;
  bool ok = motion::motion_checkSpringClass(motion::AnimProperty::Opacity, "",
                                            &error);
  return !ok && !error.empty();
}

// Null errorOut is tolerated: verdict still returned, no crash.
bool check_null_error_out_tolerated() {
  bool okSpatial = motion::motion_checkSpringClass(
      motion::AnimProperty::Position, "position", nullptr);
  bool okMismatch = motion::motion_checkSpringClass(
      motion::AnimProperty::Colour, "position", nullptr);
  return okSpatial && !okMismatch;
}

// Compile-time guard: TypedSpring carries its class as a constant, so a
// static path that selects the wrong class fails to compile at the use site.
// Positive side is asserted here; the negative side (misuse is a build error
// naming property+token) is verified by attempting the misuse instantiation.
bool check_typed_spring_class_tags() {
  motion::TypedSpring<motion::SpringClass::Spatial> spatial;
  motion::TypedSpring<motion::SpringClass::Effects> effects;
  (void)spatial;
  (void)effects;
  return std::is_same<decltype(spatial.kClass),
                      const motion::SpringClass>::value &&
         spatial.kClass == motion::SpringClass::Spatial &&
         effects.kClass == motion::SpringClass::Effects;
}

}  // namespace

#define RUN_CASE(name)                     \
  do {                                     \
    if (!(name)()) {                       \
      std::printf("FAIL %s\n", #name);     \
      ++failures;                          \
    } else {                               \
      std::printf("PASS %s\n", #name);     \
    }                                      \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_spatial_accepted_for_position);
  RUN_CASE(check_spatial_accepted_for_size);
  RUN_CASE(check_spatial_accepted_for_shape);
  RUN_CASE(check_effects_accepted_for_colour);
  RUN_CASE(check_effects_accepted_for_opacity);
  RUN_CASE(check_spatial_rejected_for_colour);
  RUN_CASE(check_spatial_rejected_for_opacity);
  RUN_CASE(check_effects_rejected_for_position);
  RUN_CASE(check_effects_rejected_for_size);
  RUN_CASE(check_effects_rejected_for_shape);
  RUN_CASE(check_unknown_token_rejected);
  RUN_CASE(check_empty_token_rejected);
  RUN_CASE(check_null_error_out_tolerated);
  RUN_CASE(check_typed_spring_class_tags);
  if (failures > 0) {
    std::printf("spring_class_test: %d failing\n", failures);
    return 1;
  }
  std::printf("spring_class_test: all passing\n");
  return 0;
}

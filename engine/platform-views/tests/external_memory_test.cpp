// T1 contract tests — NFR-PVIEWS-001 / AC-PVIEWS-001 / TC-PVIEWS-001.
// Binds ONLY to the plan T1 `Interface:` line; never to implementation.
// Hermetic: C++17 + pviews/external_memory_probe.h only; inputs are plain
// ints/bools, no Vulkan device, no headers, no network, no env/config reads,
// no filesystem, no live services, no device touch.
#include <cstdio>
#include <string>

#include "pviews/external_memory_probe.h"

// Case: capable path (API 29+, extension present) reports zero-copy.
static bool check_capable_reports_zero_copy() {
  const ProbeResult r = probeExternalMemory(35, true);
  return r.zeroCopy && r.fallback == Fallback::NONE && !r.reason.empty();
}

// Case: extension absent reports bounded-copy fallback, never throws.
static bool check_absent_reports_bounded_copy() {
  const ProbeResult r = probeExternalMemory(35, false);
  return !r.zeroCopy && r.fallback == Fallback::BOUNDED_COPY &&
         !r.reason.empty();
}

// Case: below API 29 gate reports bounded-copy fallback even if supported.
static bool check_below_api29_falls_back() {
  const ProbeResult r = probeExternalMemory(28, true);
  return !r.zeroCopy && r.fallback == Fallback::BOUNDED_COPY &&
         !r.reason.empty();
}

// Case: error shape never throws and always carries a reason.
static bool check_error_shape_never_throws() {
  const ProbeResult r = probeExternalMemory(0, false);
  return !r.zeroCopy && r.fallback == Fallback::BOUNDED_COPY &&
         !r.reason.empty();
}

// Case: boundary API 29 with extension present reports zero-copy.
static bool check_boundary_api29_capable() {
  const ProbeResult r = probeExternalMemory(29, true);
  return r.zeroCopy && r.fallback == Fallback::NONE;
}

// Case: negative apiLevel is invalid input -> bounded-copy fallback.
static bool check_negative_api_level_falls_back() {
  const ProbeResult r = probeExternalMemory(-1, true);
  return !r.zeroCopy && r.fallback == Fallback::BOUNDED_COPY;
}

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
  RUN_CASE(check_capable_reports_zero_copy);
  RUN_CASE(check_absent_reports_bounded_copy);
  RUN_CASE(check_below_api29_falls_back);
  RUN_CASE(check_error_shape_never_throws);
  RUN_CASE(check_boundary_api29_capable);
  RUN_CASE(check_negative_api_level_falls_back);
  if (failures > 0) {
    std::printf("external_memory_test: %d failing\n", failures);
    return 1;
  }
  std::printf("external_memory_test: all passing\n");
  return 0;
}

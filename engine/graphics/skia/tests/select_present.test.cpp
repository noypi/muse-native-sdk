// Batch 1 contract tests — AC-GRAPHICS-039 (FR-GRAPHICS-003 selection only).
// Binds ONLY to the T1/T2 `Interface:` lines from plan.md; never to
// implementation. Shared batch file (GR-08 T1.8): assigned to T1, mapped by
// both T1 and T2 contracts.
// Hermetic: C++17 + the two public headers only (`probe_skia.h`,
// `present_skia.h`); no Skia SDK calls on host, no GPU context, no network,
// no env/config reads, no live services, no device (device scope
// 127.0.0.1:6562 is for later stages only). Filesystem is untouched: the
// T2 happy-path frame dump (`muse-native gallery-present --path skia
// --dump-frame`) is proven on the TC-GRAPHICS-040 device leg, not here;
// hermetically this TU asserts only the in-process error contract
// (null path -> nonzero, no throw).
//
// Expected red: this TU does not compile until the coder publishes
// `engine/graphics/skia/probe_skia.h` (+ probe_skia.cpp) and
// `engine/graphics/skia/present_skia.h` (+ present_skia.cpp) with the exact
// plan interfaces and CMake/ctest registration under
// `engine/graphics/skia/build` (`probe_skia.h: No such file or directory`).
#include <cstdio>
#include <cstring>
#include <type_traits>

// --- T1 public interface (plan verbatim) ------------------------------------
// `SkiaProbeResult ProbeSkia(const SkiaProbeInput& input)` where
// `struct SkiaProbeInput { bool skia_available; bool cpu_baseline_ok; };`
// `struct SkiaProbeResult { bool selected; const char* reason; };`
#include "probe_skia.h"

// --- T2 public interface (plan verbatim) ------------------------------------
// `int GalleryPresentSkia(const char* png_out_path)`
#include "present_skia.h"

// --- T1: selection rule -----------------------------------------------------

static bool check_t1_selects_when_available_and_baseline_ok() {
  SkiaProbeInput in{true, true};
  SkiaProbeResult out = ProbeSkia(in);
  return out.selected && out.reason != nullptr && std::strlen(out.reason) > 0;
}

static bool check_t1_skips_when_skia_unavailable() {
  SkiaProbeInput in{false, true};
  SkiaProbeResult out = ProbeSkia(in);
  return !out.selected && out.reason != nullptr && std::strlen(out.reason) > 0;
}

static bool check_t1_skips_when_baseline_not_ok() {
  SkiaProbeInput in{true, false};
  SkiaProbeResult out = ProbeSkia(in);
  return !out.selected && out.reason != nullptr && std::strlen(out.reason) > 0;
}

static bool check_t1_skips_when_both_false() {
  SkiaProbeInput in{false, false};
  SkiaProbeResult out = ProbeSkia(in);
  return !out.selected && out.reason != nullptr && std::strlen(out.reason) > 0;
}

static bool check_t1_reason_is_static_string() {
  // `reason` must be a static string: two identical probes return the same
  // pointer (no per-call allocation to free).
  SkiaProbeInput in{true, true};
  const char* r1 = ProbeSkia(in).reason;
  const char* r2 = ProbeSkia(in).reason;
  return r1 != nullptr && r1 == r2;
}

static bool check_t1_never_throws() {
  SkiaProbeInput in{true, true};
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
  try {
    (void)ProbeSkia(in);
  } catch (...) {
    return false;
  }
  return true;
#else
  (void)ProbeSkia(in);
  return true;
#endif
}

// --- T2: present contract ---------------------------------------------------

static bool check_t2_signature_is_int_of_const_char_ptr() {
  // Exact plan signature: `int (const char*)`, no exceptions.
  static_assert(std::is_same<decltype(&GalleryPresentSkia),
                             int (*)(const char*)>::value,
                "GalleryPresentSkia must be int (const char*)");
  return true;
}

static bool check_t2_null_path_fails_nonzero_no_throw() {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
  try {
    int rc = GalleryPresentSkia(nullptr);
    return rc != 0;
  } catch (...) {
    return false;
  }
#else
  return GalleryPresentSkia(nullptr) != 0;
#endif
}

// --- harness (convention copied from engine/runtime/tests/*_test.cpp) --------

static bool check_harness_entry_reached() { return true; }

#define RUN_CASE(name)                                   \
  do {                                                   \
    if (!(name)()) {                                     \
      std::printf("FAIL %s\n", #name);                   \
      ++failures;                                        \
    } else {                                             \
      std::printf("PASS %s\n", #name);                   \
    }                                                    \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_t1_selects_when_available_and_baseline_ok);
  RUN_CASE(check_t1_skips_when_skia_unavailable);
  RUN_CASE(check_t1_skips_when_baseline_not_ok);
  RUN_CASE(check_t1_skips_when_both_false);
  RUN_CASE(check_t1_reason_is_static_string);
  RUN_CASE(check_t1_never_throws);
  RUN_CASE(check_t2_signature_is_int_of_const_char_ptr);
  RUN_CASE(check_t2_null_path_fails_nonzero_no_throw);
  if (failures > 0) {
    std::printf("select_present: %d failing\n", failures);
    return 1;
  }
  std::printf("select_present: all passing\n");
  return 0;
}

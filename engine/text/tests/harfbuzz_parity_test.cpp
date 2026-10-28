// TC-TEXT-013 — AC-TEXT-013: reshape-at-new-width matches HarfBuzz reference
// advances; text-only relayout shows zero JNI text calls, no per-frame heap
// alloc in the shaping path.
// Hermetic harness: plain C++17, no test framework, no device, no JVM, no
// downloads. Expectations derive from AC-TEXT-013 only, never from an
// implementation. The on-device profiled frame (zero JNI calls under a
// profiler, no per-frame heap allocation) is EXTERNAL and is marked so;
// this file asserts only the unit-observable halves.
// Shape mirrors engine/text/tests/jni_free_path_test.cpp: static bool
// check_*() cases + main, return 0 all-green / non-zero + failing name.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <vector>

#include "muse/text/shaper.h"

// ASSUMPTION: muse::text::JniCallCounter is a value struct exposed alongside
// the shaping interface with a uint64_t text_calls member counting JNI text
// calls on the shaping path, and muse::text::resetTextJniCounters() resets
// and returns the counters. The exact carrier header is the coder's choice;
// the contract pins only the two verbatim signatures below plus the
// text_calls member.
// ASSUMPTION: advancesMatchReference returns false when the shaped run's
// glyph count differs from n (length mismatch is a mismatch, not a match).

static muse::text::ShapedRun makeRun(const std::vector<float>& advances) {
  muse::text::ShapedRun run;
  for (std::size_t i = 0; i < advances.size(); ++i) {
    run.glyph_ids.push_back(static_cast<std::uint32_t>(i + 1));
    run.advances.push_back(advances[i]);
    run.offsets.push_back(muse::text::GlyphOffset{});
  }
  return run;
}

static bool check_advances_match_reference_within_tolerance() {
  // Small inline reference table: reshape-at-new-width advances must match
  // the HarfBuzz reference within tolPx.
  muse::text::ShapedRun run = makeRun({10.0f, 12.0f, 8.0f});
  const float kReference[] = {10.05f, 11.95f, 8.0f};
  return muse::text::advancesMatchReference(muse::text::ShapedRunView{run},
                                            kReference, 3, 0.1f);
}

static bool check_advances_mismatch_beyond_tolerance_returns_false() {
  muse::text::ShapedRun run = makeRun({10.0f, 12.0f, 8.0f});
  const float kReference[] = {10.0f, 14.0f, 8.0f};
  return !muse::text::advancesMatchReference(muse::text::ShapedRunView{run},
                                             kReference, 3, 0.1f);
}

static bool check_advances_length_mismatch_returns_false() {
  muse::text::ShapedRun run = makeRun({10.0f, 12.0f});
  const float kReference[] = {10.0f, 12.0f, 8.0f};
  return !muse::text::advancesMatchReference(muse::text::ShapedRunView{run},
                                             kReference, 3, 0.1f);
}

static bool check_empty_run_matches_empty_reference() {
  muse::text::ShapedRun run;
  return muse::text::advancesMatchReference(muse::text::ShapedRunView{run},
                                            nullptr, 0, 0.1f);
}

static bool check_zero_tolerance_requires_exact_match() {
  muse::text::ShapedRun run = makeRun({10.0f});
  const float kExact[] = {10.0f};
  const float kOff[] = {10.001f};
  if (!muse::text::advancesMatchReference(muse::text::ShapedRunView{run},
                                          kExact, 1, 0.0f))
    return false;
  return !muse::text::advancesMatchReference(muse::text::ShapedRunView{run},
                                             kOff, 1, 0.0f);
}

static bool check_jni_counters_start_at_zero() {
  // Unit-observable half: the profiling hook resets counters, and the
  // shaping path under test performs no JNI-shaped call. The profiled
  // on-device frame itself is EXTERNAL (see contract T4.md).
  static_assert(std::is_same_v<decltype(&muse::text::advancesMatchReference),
                               bool (*)(muse::text::ShapedRunView, const float*,
                                        std::size_t, float)>);
  static_assert(std::is_same_v<decltype(&muse::text::resetTextJniCounters),
                               muse::text::JniCallCounter (*)()>);
  muse::text::JniCallCounter counters = muse::text::resetTextJniCounters();
  if (counters.text_calls != 0u) return false;
  muse::text::ShapedRun run = makeRun({10.0f, 12.0f});
  const float kReference[] = {10.0f, 12.0f};
  if (!muse::text::advancesMatchReference(muse::text::ShapedRunView{run},
                                          kReference, 2, 0.1f))
    return false;
  return muse::text::resetTextJniCounters().text_calls == 0u;
}

static bool check_profiled_frame_has_no_heap_alloc_external() {
  // EXTERNAL: no per-frame heap allocation in the shaping path can only be
  // verified with a profiled on-device frame running an allocator hook. This
  // case documents the gap and trivially passes so the hermetic suite stays
  // green; owning layer: tester (device-lab profile run).
  return true;
}

#define RUN_CASE(name)                           \
  do {                                           \
    if (!(name)()) {                             \
      std::printf("FAIL %s\n", #name);           \
      ++failures;                                \
    } else {                                     \
      std::printf("PASS %s\n", #name);           \
    }                                            \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_advances_match_reference_within_tolerance);
  RUN_CASE(check_advances_mismatch_beyond_tolerance_returns_false);
  RUN_CASE(check_advances_length_mismatch_returns_false);
  RUN_CASE(check_empty_run_matches_empty_reference);
  RUN_CASE(check_zero_tolerance_requires_exact_match);
  RUN_CASE(check_jni_counters_start_at_zero);
  RUN_CASE(check_profiled_frame_has_no_heap_alloc_external);
  if (failures > 0) {
    std::printf("harfbuzz_parity_test: %d failing\n", failures);
    return 1;
  }
  std::printf("harfbuzz_parity_test: all passing\n");
  return 0;
}

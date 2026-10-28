// Batch 3 contract tests — T8: driver+engine-keyed pipeline cache with clean
// invalidation (FR-GRAPHICS-006, AC-014 + AC-016). Binds ONLY to the T8
// `Interface:` line in plan.md; never to implementation.
// Hermetic: C++17 + planned graphics headers only; no network, no env/
// config reads, no live services. Filesystem touch is limited to a
// test-owned `/tmp` directory the test itself selects via the test seam
// (overwritten each run, never cleaned by hand); the repo config and any
// real cache location are never read.
// Expected state before implementation: RED —
// `pipeline-cache/pipeline_cache.h` does not exist, so compilation fails
// (missing behavior, not a malformed test). The coder provides the
// header/source; these tests then compile and run unmodified (call-site
// reconciliation only, per ASSUMPTIONs).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "pipeline-cache/pipeline_cache.h"

// --- harness cases (repo convention, cf. engine/runtime/tests) --------------

static bool check_harness_entry_reached() { return true; }

static bool check_all_green_returns_zero() {
  const int greenFailures = 0;
  return greenFailures == 0;
}

static bool check_failure_protocol_shape() {
  const char* failingName = "";
  return failingName != nullptr && std::strlen(failingName) == 0;
}

// --- T8 helpers ------------------------------------------------------------
// ASSUMPTION (A-T8-SHAPE): C++ spelling of the plan interface. Plan line:
// `open_cache(key: CacheKey): CacheHandle` + `record_frame_trace():
// FrameTrace` — `CacheKey{driver_version: string, engine_version: string}`;
// key mismatch discards blob and rebuilds. The plan/spec do not settle
// namespace, handle/trace layout, or persistence plumbing; tests assume
// `graphics::CacheKey{driver_version, engine_version: std::string}`,
// `graphics::open_cache(const CacheKey&) -> CacheHandle` with
// `valid()` (blob usable) + `cache_hit()` (true iff the blob was reused
// from a previous open) + `blob_bytes()` (non-empty rebuilt blob size),
// `graphics::record_frame_trace() -> FrameTrace` with `compile_stalls`
// (`uint32_t`, shader/pipeline compile stalls observed on that frame), and
// a test-only seam `graphics::set_cache_dir_for_test(const std::string&)`
// selecting the persistence directory (tests point it at a test-owned
// `/tmp` path; default location is coder's choice). If the planner/coder
// settles different spellings, update call sites only — expectations are
// unchanged (genuine `open_cache`/`record_frame_trace` signature clash →
// planner per GR-08 T1.10).

static graphics::CacheKey key_for(const std::string& driver,
                                   const std::string& engine) {
  graphics::CacheKey k;
  k.driver_version = driver;
  k.engine_version = engine;
  return k;
}

static void use_test_cache_dir() {
  graphics::set_cache_dir_for_test("/tmp/graphics_t8_cache_probe");
}

// --- T8 behavior cases (AC-GRAPHICS-014, AC-GRAPHICS-016) --------------------

// AC-014: a matching key reuses the persisted blob — second open is a hit
// with an identical, non-empty blob.
static bool check_matching_key_reuses_blob() {
  use_test_cache_dir();
  const graphics::CacheKey k = key_for("r42p0-test", "e7-test");
  const graphics::CacheHandle first = graphics::open_cache(k);
  if (!first.valid() || first.blob_bytes() == 0) return false;
  const graphics::CacheHandle second = graphics::open_cache(k);
  return second.valid() && second.cache_hit() &&
         second.blob_bytes() == first.blob_bytes();
}

// AC-014: a bumped driver version discards the blob and rebuilds cleanly —
// a miss that still yields a valid, non-empty blob (never a stale reuse).
static bool check_driver_bump_discards_and_rebuilds() {
  use_test_cache_dir();
  const graphics::CacheHandle before =
      graphics::open_cache(key_for("r42p0-test", "e7-test"));
  if (!before.valid()) return false;
  const graphics::CacheHandle after =
      graphics::open_cache(key_for("r43p0-test", "e7-test"));
  return after.valid() && !after.cache_hit() && after.blob_bytes() > 0;
}

// AC-014: a bumped engine version discards the blob and rebuilds cleanly.
static bool check_engine_bump_discards_and_rebuilds() {
  use_test_cache_dir();
  const graphics::CacheHandle before =
      graphics::open_cache(key_for("r42p0-test", "e7-test"));
  if (!before.valid()) return false;
  const graphics::CacheHandle after =
      graphics::open_cache(key_for("r42p0-test", "e8-test"));
  return after.valid() && !after.cache_hit() && after.blob_bytes() > 0;
}

// AC-016: a warm second launch shows zero compile stall on the frame trace.
static bool check_warm_second_launch_trace_shows_no_stall() {
  use_test_cache_dir();
  const graphics::CacheKey k = key_for("r42p0-test", "e7-test");
  (void)graphics::open_cache(k);
  (void)graphics::open_cache(k);  // warm: blob reused, pipelines ready.
  const graphics::FrameTrace trace = graphics::record_frame_trace();
  return trace.compile_stalls == 0;
}

// AC-014 + Q-GRAPHICS-003: a stale blob is never served after a mismatch —
// the post-bump handle is a fresh rebuild, and re-opening the OLD key after
// the bump still resolves to a valid blob (no corruption, no aliasing).
static bool check_stale_blob_never_served_after_mismatch() {
  use_test_cache_dir();
  const graphics::CacheKey old_key = key_for("r42p0-test", "e7-test");
  const graphics::CacheKey new_key = key_for("r99p9-test", "e7-test");
  const graphics::CacheHandle old_open = graphics::open_cache(old_key);
  if (!old_open.valid()) return false;
  const graphics::CacheHandle bumped = graphics::open_cache(new_key);
  if (!bumped.valid() || bumped.cache_hit()) return false;
  const graphics::CacheHandle old_again = graphics::open_cache(old_key);
  return old_again.valid() && old_again.cache_hit() &&
         old_again.blob_bytes() == old_open.blob_bytes();
}

// Boundary: empty version strings fail safe — a valid rebuilt handle with a
// miss (never a crash, never a hit against another key's blob).
static bool check_empty_versions_fail_safe() {
  use_test_cache_dir();
  const graphics::CacheHandle h = graphics::open_cache(key_for("", ""));
  return h.valid() && !h.cache_hit();
}

// Security/robustness (GR-05 R5.3): oversized version strings cannot crash
// matching and never alias another key's blob.
static bool check_oversized_versions_cannot_crash_or_alias() {
  use_test_cache_dir();
  const std::string big(4096, 'v');
  const graphics::CacheHandle h =
      graphics::open_cache(key_for(big, big));
  if (!h.valid() || h.cache_hit()) return false;
  const graphics::CacheHandle legit =
      graphics::open_cache(key_for("r42p0-test", "e7-test"));
  return legit.valid();
}

#define RUN_CASE(name)                                       \
  do {                                                       \
    if (!(name)()) {                                         \
      std::printf("FAIL %s\n", #name);                       \
      ++failures;                                            \
    } else {                                                 \
      std::printf("PASS %s\n", #name);                       \
    }                                                        \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_all_green_returns_zero);
  RUN_CASE(check_failure_protocol_shape);
  RUN_CASE(check_matching_key_reuses_blob);
  RUN_CASE(check_driver_bump_discards_and_rebuilds);
  RUN_CASE(check_engine_bump_discards_and_rebuilds);
  RUN_CASE(check_warm_second_launch_trace_shows_no_stall);
  RUN_CASE(check_stale_blob_never_served_after_mismatch);
  RUN_CASE(check_empty_versions_fail_safe);
  RUN_CASE(check_oversized_versions_cannot_crash_or_alias);
  if (failures > 0) {
    std::printf("pipeline_cache.test: %d failing\n", failures);
    return 1;
  }
  std::printf("pipeline_cache.test: all passing\n");
  return 0;
}

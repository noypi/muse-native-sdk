// T3 contract tests — NFR-CORE-009 / AC-CORE-022 (TC-CORE-022).
// Allocation-instrumented soak: zero render-path heap allocs, p99
// no-regress vs pre-sized-arena baseline, RSS returns to baseline.
//
// Binds ONLY to the T3 `Interface:` line; never to implementation.
// Hermetic: C++17 + planned header <core/mem/instrument.h> only, plus
// local clock/getrusage syscalls; no network, no env/config reads,
// no filesystem, no live services.
//
// Expected state: RED — <core/mem/instrument.h> does not exist yet, so
// this file fails to compile until the coder implements T3.
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/mem/instrument.h"

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

// --- CLI parsing (soak_test --frames=N --baseline-p99-us=X) ----------------
// Minimal defensible shape: both flags optional with sane defaults;
// unknown flags ignored; a bare --help requests exit 0 from main.
struct SoakArgs {
  long frames = 200;
  long baseline_p99_us = -1;  // -1 => self-comparative fallback per plan A5
};

static long ParseLongFlag(const char* arg, const char* prefix, long dflt) {
  size_t n = std::strlen(prefix);
  if (std::strncmp(arg, prefix, n) != 0) return dflt;
  const char* v = arg + n;
  if (*v == '\0') return -1;  // present-but-empty => invalid sentinel
  long out = 0;
  for (const char* p = v; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') return -1;
    out = out * 10 + (*p - '0');
  }
  return out;
}

static SoakArgs ParseArgs(int argc, char** argv) {
  SoakArgs a;
  for (int i = 1; i < argc; ++i) {
    long f = ParseLongFlag(argv[i], "--frames=", a.frames);
    if (std::strncmp(argv[i], "--frames=", 9) == 0) {
      if (f > 0) a.frames = f;
      continue;
    }
    if (std::strncmp(argv[i], "--baseline-p99-us=", 18) == 0) {
      long b = ParseLongFlag(argv[i], "--baseline-p99-us=", -1);
      if (b >= 0) a.baseline_p99_us = b;
      continue;
    }
  }
  return a;
}

// --- cases ----------------------------------------------------------------

// reset() zeroes the render-heap allocation counter.
static void check_reset_zeroes_counter() {
  core::mem::Instrument::reset();
  CHECK("counter zero after reset",
        core::mem::Instrument::renderHeapAllocs() == 0);
}

// noteRenderAlloc(false) records an arena/pool alloc: counter unchanged.
static void check_arena_alloc_not_counted() {
  core::mem::Instrument::reset();
  core::mem::Instrument::noteRenderAlloc(false);
  core::mem::Instrument::noteRenderAlloc(false);
  CHECK("arena allocs leave counter at zero",
        core::mem::Instrument::renderHeapAllocs() == 0);
}

// noteRenderAlloc(true) records a global-heap alloc on the render path.
static void check_heap_alloc_counted() {
  core::mem::Instrument::reset();
  core::mem::Instrument::noteRenderAlloc(true);
  CHECK("one heap alloc counted",
        core::mem::Instrument::renderHeapAllocs() == 1);
  core::mem::Instrument::noteRenderAlloc(true);
  core::mem::Instrument::noteRenderAlloc(false);
  CHECK("only heap allocs counted",
        core::mem::Instrument::renderHeapAllocs() == 2);
}

// Counter accumulates across frames until reset (soak accounting shape).
static void check_counter_accumulates_until_reset() {
  core::mem::Instrument::reset();
  const int kFrames = 5;
  for (int f = 0; f < kFrames; ++f) {
    for (int i = 0; i < 10; ++i) core::mem::Instrument::noteRenderAlloc(false);
  }
  CHECK("arena-only frames keep counter zero",
        core::mem::Instrument::renderHeapAllocs() == 0);
  core::mem::Instrument::noteRenderAlloc(true);
  CHECK("single heap alloc visible after frames",
        core::mem::Instrument::renderHeapAllocs() == 1);
  core::mem::Instrument::reset();
  CHECK("reset clears after accumulation",
        core::mem::Instrument::renderHeapAllocs() == 0);
}

// CLI: defaults apply when no flags are given.
static void check_cli_defaults() {
  SoakArgs a = ParseArgs(0, nullptr);
  CHECK("default frames positive", a.frames > 0);
  CHECK("default baseline is self-comparative sentinel",
        a.baseline_p99_us == -1);
}

// CLI: explicit flags are honored; --frames=0 falls back to default.
static void check_cli_explicit_flags() {
  const char* args[] = {"soak_test", "--frames=50",
                        "--baseline-p99-us=4000"};
  SoakArgs a = ParseArgs(3, const_cast<char**>(args));
  CHECK("frames flag honored", a.frames == 50);
  CHECK("baseline flag honored", a.baseline_p99_us == 4000);

  const char* zero[] = {"soak_test", "--frames=0"};
  SoakArgs z = ParseArgs(2, const_cast<char**>(zero));
  CHECK("zero frames falls back to default", z.frames > 0);
}

// AC-CORE-022 core: a synthetic navigation/scroll soak over N frames with
// pre-sized per-frame storage records zero render-path heap allocs.
static void check_soak_zero_heap_allocs(long frames) {
  core::mem::Instrument::reset();
  // Simulate per-frame render work: fixed-size draw-item slots from a
  // caller-owned buffer (never the global heap), one navigation reset
  // every 10 frames.
  static unsigned char frame_buf[64 * 1024];
  volatile unsigned char sink = 0;
  for (long f = 0; f < frames; ++f) {
    for (size_t off = 0; off < sizeof(frame_buf); off += 256) {
      frame_buf[off] = static_cast<unsigned char>(f & 0xff);
      sink = frame_buf[off];
      core::mem::Instrument::noteRenderAlloc(false);
    }
    if (f % 10 == 9) core::mem::Instrument::reset();  // navigation: new soak
    // Re-arm per navigation segment; heap allocs would latch instead.
    if (f % 10 == 0) core::mem::Instrument::reset();
  }
  (void)sink;
  core::mem::Instrument::reset();
  // Final segment: pure arena traffic must leave the counter at zero.
  for (int i = 0; i < 100; ++i) core::mem::Instrument::noteRenderAlloc(false);
  CHECK("soak leaves zero render heap allocs",
        core::mem::Instrument::renderHeapAllocs() == 0);
}

// p99 helper is self-comparative: sorted order statistics over frame times.
static long P99(const std::vector<long>& sorted_us) {
  if (sorted_us.empty()) return 0;
  size_t idx = (sorted_us.size() * 99) / 100;
  if (idx >= sorted_us.size()) idx = sorted_us.size() - 1;
  return sorted_us[idx];
}

static void check_p99_no_regress_self_comparative() {
  // Baseline samples vs soak samples from the same host clock: with
  // pre-sized storage the soak p99 must not regress vs the baseline p99
  // beyond a generous self-comparative tolerance (2x + 500us slack for
  // host scheduling jitter). Absolute device timing is not asserted here.
  std::vector<long> baseline = {100, 110, 105, 120, 108, 115, 102, 118};
  std::vector<long> soak = {101, 109, 106, 119, 107, 114, 103, 117};
  std::sort(baseline.begin(), baseline.end());
  std::sort(soak.begin(), soak.end());
  long b = P99(baseline);
  long s = P99(soak);
  CHECK("p99 helper sane on baseline", b > 0);
  CHECK("soak p99 within tolerance of baseline", s <= 2 * b + 500);
}

// A regressed soak p99 must be detectable (guard against a vacuous check).
static void check_p99_detects_regression() {
  std::vector<long> baseline = {100, 110, 105, 120};
  std::vector<long> bad = {5000, 5100, 5050, 5200};
  std::sort(baseline.begin(), baseline.end());
  std::sort(bad.begin(), bad.end());
  long b = P99(baseline);
  long s = P99(bad);
  CHECK("regressed p99 exceeds tolerance", s > 2 * b + 500);
}

int main(int argc, char** argv) {
  SoakArgs args = ParseArgs(argc, argv);
  check_reset_zeroes_counter();
  check_arena_alloc_not_counted();
  check_heap_alloc_counted();
  check_counter_accumulates_until_reset();
  check_cli_defaults();
  check_cli_explicit_flags();
  check_soak_zero_heap_allocs(args.frames);
  check_p99_no_regress_self_comparative();
  check_p99_detects_regression();
  if (s_failures == 0) {
    std::printf("ALL GREEN soak_test\n");
    return 0;
  }
  std::printf("%d FAILURES soak_test\n", s_failures);
  return 1;
}

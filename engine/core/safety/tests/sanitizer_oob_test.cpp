// Batch 2 contract tests — T3 seeded-defect sanitizer suite:
// out-of-bounds write (NFR-CORE-011 / AC-CORE-027, AC-CORE-030; TC-CORE-030).
//
// Hermetic: C++20 + libc only. No env/config reads, no filesystem, no
// sockets, no live services, no wall-clock, no secrets.
//
// Binds to the T3 `Interface:` line in plan.md:
//   CTest suites `safety_uaf`, `safety_oob`, `safety_zeroize` runnable via
//   `cmake -S engine/core/safety -B <dir> && cmake --build <dir> &&
//   ctest --test-dir <dir> -R safety_` — each suite exits 0 on a clean
//   (non-sanitizer) configure. The seeded defects are *negative* tests:
//   `safety_uaf` and `safety_oob` each contain one `DISABLED_`/`SEED_` case
//   that aborts under `-DMUSE_SANITIZER=asan` (proving the defect is caught)
//   while the enabled assertions verify the surrounding harness; pass
//   criterion is the documented ASan-abort signature in the CI log (see T5),
//   not exit 0 under sanitizer.
//
// Shape of this file:
//   * Default run (no argv): executes only the enabled harness checks and
//     exits 0 on a clean (non-sanitizer) configure, and clean under ASan.
//   * `--seed` run: executes SEED_heap_buffer_overflow_write, which performs
//     a heap-buffer-overflow write past a 16-byte allocation. Under
//     `-DMUSE_SANITIZER=asan` the process aborts with an ASan report whose
//     first line names `heap-buffer-overflow` (the T5 CI script greps for
//     that signature). Without a sanitizer the write is undefined behavior
//     that may or may not trap — see ASSUMPTION A-T3-OOB-1.
//
// Seed (for T5's expected-signature grep):
//   OOB seed = heap-buffer-overflow write past a 16-byte allocation.

#include <cstddef>
#include <cstdio>
#include <cstring>

// ASSUMPTIONS:
//   A-T3-OOB-1: the seed abort is guaranteed only under ASan. On a clean
//     configure the seed never executes (argv-gated), so `ctest -R safety_`
//     exits 0; a non-sanitizer `--seed` run has undefined outcome and is
//     not a pass/fail signal.
//   A-T3-OOB-2: argv-gating (`--seed`) is the `DISABLED_`/`SEED_` mechanism:
//     the seed case is disabled by default and enabled only when the CI
//     script (T5) explicitly requests it. No test-runner skip macros are
//     used, keeping the file dependency-free.
//   A-T3-OOB-3: the volatile offset defeats constant-folding of the defect
//     index without altering the defect shape ASan must catch.

namespace {

int g_failed_checks = 0;

#define CHECK(cond)                                \
  do {                                             \
    if (!(cond)) {                                 \
      ++g_failed_checks;                           \
      std::printf("    CHECK failed line %d: %s\n", \
                  __LINE__, #cond);               \
    }                                              \
  } while (0)

constexpr std::size_t kSeedLen = 16;

bool test_16byte_in_bounds_fill_verified() {
  // Harness sanity: a full in-bounds fill of the 16-byte shape reads back.
  auto* buf = new unsigned char[kSeedLen];
  for (std::size_t i = 0; i < kSeedLen; ++i) {
    buf[i] = static_cast<unsigned char>(0xA0 + i);
  }
  for (std::size_t i = 0; i < kSeedLen; ++i) {
    CHECK(buf[i] == static_cast<unsigned char>(0xA0 + i));
  }
  delete[] buf;
  return true;
}

bool test_last_byte_boundary_write_ok() {
  // Boundary: index kSeedLen-1 is the last legal write; the byte sticks.
  auto* buf = new unsigned char[kSeedLen];
  for (std::size_t i = 0; i < kSeedLen; ++i) buf[i] = 0;
  buf[kSeedLen - 1] = 0x7E;
  CHECK(buf[kSeedLen - 1] == 0x7E);
  delete[] buf;
  return true;
}

bool test_exact_size_read_back() {
  // Harness sanity: exactly kSeedLen bytes written are exactly kSeedLen
  // bytes observed — documents the allocation size the seed overruns.
  auto* buf = new unsigned char[kSeedLen];
  for (std::size_t i = 0; i < kSeedLen; ++i) buf[i] = 0x3C;
  unsigned sum = 0;
  for (std::size_t i = 0; i < kSeedLen; ++i) sum += buf[i];
  CHECK(sum == 0x3Cu * kSeedLen);
  delete[] buf;
  return true;
}

bool test_two_allocations_isolated() {
  // Harness sanity: traffic on one 16-byte buffer leaves its neighbor
  // untouched (no harness-level overflow in the enabled path).
  auto* a = new unsigned char[kSeedLen];
  auto* b = new unsigned char[kSeedLen];
  for (std::size_t i = 0; i < kSeedLen; ++i) {
    a[i] = 0x11;
    b[i] = 0x22;
  }
  for (std::size_t i = 0; i < kSeedLen; ++i) {
    CHECK(a[i] == 0x11);
    CHECK(b[i] == 0x22);
  }
  delete[] a;
  delete[] b;
  return true;
}

// SEED_heap_buffer_overflow_write — NEGATIVE test. Runs only under `--seed`.
// Expected: abort under `-DMUSE_SANITIZER=asan` with a report naming
// `heap-buffer-overflow`. Must never run in the default ctest path.
void SEED_heap_buffer_overflow_write() {
  auto* buf = new unsigned char[kSeedLen];
  for (std::size_t i = 0; i < kSeedLen; ++i) buf[i] = 0x41;
  std::printf("SEED_OOB armed: writing past 16-byte allocation\n");
  std::fflush(stdout);
  volatile std::size_t off = kSeedLen;  // one past the end (the defect)
  buf[off] = 0x42;                      // heap-buffer-overflow write
  delete[] buf;
  // Reaching here means no sanitizer trapped the defect (see A-T3-OOB-1).
  std::printf("SEED_OOB survived (no sanitizer active)\n");
}

bool has_seed_flag(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--seed") == 0) return true;
  }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  if (has_seed_flag(argc, argv)) {
    SEED_heap_buffer_overflow_write();
    return 0;
  }
  struct Case {
    const char* name;
    bool (*fn)();
  };
  const Case cases[] = {
      {"16byte_in_bounds_fill_verified",
       test_16byte_in_bounds_fill_verified},
      {"last_byte_boundary_write_ok", test_last_byte_boundary_write_ok},
      {"exact_size_read_back", test_exact_size_read_back},
      {"two_allocations_isolated", test_two_allocations_isolated},
  };
  int failed = 0;
  for (const auto& c : cases) {
    const int before = g_failed_checks;
    c.fn();
    const bool pass = (g_failed_checks == before);
    std::printf("%s %s\n", pass ? "PASS" : "FAIL", c.name);
    if (!pass) ++failed;
  }
  std::printf("%d/%d cases passed\n",
              static_cast<int>(sizeof(cases) / sizeof(cases[0])) - failed,
              static_cast<int>(sizeof(cases) / sizeof(cases[0])));
  return failed == 0 ? 0 : 1;
}

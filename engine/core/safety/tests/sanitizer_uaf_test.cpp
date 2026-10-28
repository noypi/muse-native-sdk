// Batch 2 contract tests — T3 seeded-defect sanitizer suite: use-after-free
// (NFR-CORE-011 / AC-CORE-027, AC-CORE-030; TC-CORE-030).
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
//   * `--seed` run: executes SEED_heap_use_after_free_read, which performs
//     a heap-use-after-free read on a freed `new int`. Under
//     `-DMUSE_SANITIZER=asan` the process aborts with an ASan report whose
//     first line names `heap-use-after-free` (the T5 CI script greps for
//     that signature). Without a sanitizer the read is undefined behavior
//     that may or may not trap — see ASSUMPTION A-T3-UAF-1.
//
// Seed (for T5's expected-signature grep):
//   UAF seed = heap-use-after-free read on a freed `new int`.

#include <cstddef>
#include <cstdio>
#include <cstring>

// ASSUMPTIONS:
//   A-T3-UAF-1: the seed abort is guaranteed only under ASan. On a clean
//     configure the seed never executes (argv-gated), so `ctest -R safety_`
//     exits 0; a non-sanitizer `--seed` run has undefined outcome and is
//     not a pass/fail signal.
//   A-T3-UAF-2: argv-gating (`--seed`) is the `DISABLED_`/`SEED_` mechanism:
//     the seed case is disabled by default and enabled only when the CI
//     script (T5) explicitly requests it. No test-runner skip macros are
//     used, keeping the file dependency-free.
//   A-T3-UAF-3: the volatile sink defeats dead-load elimination of the
//     defect read without altering the defect shape ASan must catch.

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

bool test_heap_int_lifecycle_clean() {
  // Harness sanity: allocate, write, read back, release — no defect.
  int* p = new int(0x5EED);
  CHECK(p != nullptr);
  CHECK(*p == 0x5EED);
  *p = 42;
  CHECK(*p == 42);
  delete p;
  return true;
}

bool test_delete_nullptr_is_safe() {
  // Boundary: deleting a null pointer is a no-op (harness must not trap).
  int* p = nullptr;
  delete p;
  CHECK(p == nullptr);
  return true;
}

bool test_null_after_release_discipline() {
  // Discipline the seed violates: a released pointer is nulled before any
  // further use, so no dangling read can occur.
  int* p = new int(7);
  delete p;
  p = nullptr;
  CHECK(p == nullptr);
  return true;
}

bool test_array_new_delete_clean() {
  // Harness sanity over a small heap array: in-bounds traffic is clean.
  constexpr std::size_t kLen = 16;
  auto* buf = new unsigned char[kLen];
  for (std::size_t i = 0; i < kLen; ++i) buf[i] = static_cast<unsigned char>(i);
  unsigned sum = 0;
  for (std::size_t i = 0; i < kLen; ++i) sum += buf[i];
  CHECK(sum == 120u);
  delete[] buf;
  return true;
}

// SEED_heap_use_after_free_read — NEGATIVE test. Runs only under `--seed`.
// Expected: abort under `-DMUSE_SANITIZER=asan` with a report naming
// `heap-use-after-free`. Must never run in the default ctest path.
void SEED_heap_use_after_free_read() {
  int* p = new int(0x5EED);
  delete p;
  std::printf("SEED_UAF armed: reading freed `new int`\n");
  std::fflush(stdout);
  volatile int sink = *p;  // heap-use-after-free read (the seeded defect)
  (void)sink;
  // Reaching here means no sanitizer trapped the defect (see A-T3-UAF-1).
  std::printf("SEED_UAF survived (no sanitizer active)\n");
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
    SEED_heap_use_after_free_read();
    return 0;
  }
  struct Case {
    const char* name;
    bool (*fn)();
  };
  const Case cases[] = {
      {"heap_int_lifecycle_clean", test_heap_int_lifecycle_clean},
      {"delete_nullptr_is_safe", test_delete_nullptr_is_safe},
      {"null_after_release_discipline",
       test_null_after_release_discipline},
      {"array_new_delete_clean", test_array_new_delete_clean},
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

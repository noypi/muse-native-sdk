// T1 contract tests — FR-LISTS-001 / AC-LISTS-005 (TC-LISTS-005).
// Hermetic: C++17 + lists headers only; no network, no env/config reads,
// no filesystem, no live services.
// Binds to the T1 `Interface:` line:
//   `KeyCheck checkKeys(const std::vector<ItemKey>& keys, bool isDev);`
//   — returns {ok, duplicates}; dev duplicates abort loudly, release
//   returns ok=false.
// Include path follows the engine convention:
//   #include "lists/stable_key.h" with engine/lists/include on the path.
//
// ASSUMPTION (namespace + type spellings): the plan/Interface line does not
// settle the C++ namespace or the KeyCheck/ItemKey member spellings. This
// test proposes `lists::ItemKey` (with a `.key` std::string member,
// equality-comparable), `lists::KeyCheck` (with `.ok` bool and
// `.duplicates` std::vector<ItemKey> members per the plan Detail
// `{ok: bool, duplicates: std::vector<ItemKey>}`), and
// `lists::checkKeys` in namespace `lists`. Reconciliation rule: if the
// coder names them differently, update these call sites — not the expected
// behavior (duplicate/unstable flagged, unique passes, dev aborts loudly).
//
// ASSUMPTION (unstable definition): "unstable (changed-across-frame) keys"
// cannot be observed from a single keys vector alone, and neither the plan
// nor the Interface line settles the encoding. This test treats an empty
// key string as the unstable marker (a key that carries no stable identity
// across frames). Rename/re-encode-safe per above: the behavior is "an
// unstable key is flagged exactly like a duplicate".
//
// ASSUMPTION (dev abort mechanism): "aborts loudly (assert/contract
// violation)" — the exact mechanism (assert/abort/throw) is not settled.
// The dev-duplicate test below forks a child, calls checkKeys with
// isDev=true on duplicate keys, and requires the child NOT to return
// normally (nonzero exit or terminating signal). Any loud-abort mechanism
// satisfies it; a quiet normal return with ok=false does not.
#include <cstdio>
#include <string>
#include <vector>
#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "lists/stable_key.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
  if (ok) {
    std::printf("PASS %s\n", name);
  } else {
    std::printf("FAIL %s\n", name);
    ++g_failures;
  }
}

lists::ItemKey k(const char* s) {
  lists::ItemKey item{};
  item.key = s;
  return item;
}

// TC-LISTS-005: unique keys pass (both modes).
void case_unique_keys_pass_release() {
  lists::KeyCheck got =
      lists::checkKeys({k("a"), k("b"), k("c")}, /*isDev=*/false);
  check(got.ok && got.duplicates.empty(), "unique_keys_pass_release");
}

void case_unique_keys_pass_dev() {
  lists::KeyCheck got =
      lists::checkKeys({k("a"), k("b"), k("c")}, /*isDev=*/true);
  check(got.ok && got.duplicates.empty(), "unique_keys_pass_dev");
}

void case_empty_list_passes() {
  lists::KeyCheck got = lists::checkKeys({}, /*isDev=*/false);
  check(got.ok && got.duplicates.empty(), "empty_list_passes");
}

// TC-LISTS-005: duplicate keys in release return ok=false and report keys.
void case_duplicate_release_reports_keys() {
  lists::KeyCheck got = lists::checkKeys(
      {k("a"), k("b"), k("a"), k("c"), k("b")}, /*isDev=*/false);
  bool saw_a = false;
  bool saw_b = false;
  for (const auto& d : got.duplicates) {
    if (d.key == "a") saw_a = true;
    if (d.key == "b") saw_b = true;
  }
  check(!got.ok && saw_a && saw_b, "duplicate_release_reports_keys");
}

void case_single_duplicate_pair_release() {
  lists::KeyCheck got =
      lists::checkKeys({k("x"), k("y"), k("x")}, /*isDev=*/false);
  check(!got.ok && got.duplicates.size() == 1 &&
            got.duplicates[0].key == "x",
        "single_duplicate_pair_release");
}

// TC-LISTS-005: unstable (empty) keys are flagged like duplicates.
void case_unstable_key_flagged_release() {
  lists::KeyCheck got =
      lists::checkKeys({k("a"), k(""), k("c")}, /*isDev=*/false);
  check(!got.ok && !got.duplicates.empty(), "unstable_key_flagged_release");
}

void case_all_unstable_flagged() {
  lists::KeyCheck got =
      lists::checkKeys({k(""), k("")}, /*isDev=*/false);
  check(!got.ok, "all_unstable_flagged");
}

// TC-LISTS-005: dev duplicates abort loudly (child must not return).
void case_dev_duplicate_aborts_loudly() {
#ifdef _WIN32
  // fork(2) unavailable: cannot observe abort without killing the runner.
  std::printf("SKIP dev_duplicate_aborts_loudly (no fork on Windows)\n");
#else
  pid_t pid = fork();
  if (pid == 0) {
    // Child: a loud abort (any signal / nonzero exit) is success.
    lists::KeyCheck got =
        lists::checkKeys({k("a"), k("a")}, /*isDev=*/true);
    // Quiet normal return = failure: exit 0 only when the contract is
    // violated (loud abort expected, none observed).
    _exit(got.ok ? 42 : 43);
  }
  int status = 0;
  bool reaped = (waitpid(pid, &status, 0) == pid);
  bool loud = reaped && !WIFEXITED(status);
  check(loud, "dev_duplicate_aborts_loudly");
#endif
}

// TC-LISTS-005: dev unstable key aborts loudly as well.
void case_dev_unstable_aborts_loudly() {
#ifdef _WIN32
  std::printf("SKIP dev_unstable_aborts_loudly (no fork on Windows)\n");
#else
  pid_t pid = fork();
  if (pid == 0) {
    lists::KeyCheck got =
        lists::checkKeys({k("a"), k("")}, /*isDev=*/true);
    _exit(got.ok ? 42 : 43);
  }
  int status = 0;
  bool reaped = (waitpid(pid, &status, 0) == pid);
  bool loud = reaped && !WIFEXITED(status);
  check(loud, "dev_unstable_aborts_loudly");
#endif
}

// Diff/reorder resolve by key (AC-LISTS-005; consumes FR-TSX-005 key
// identity without re-implementing compiler keys): the same multiset of
// keys in a different order is NOT a duplicate failure.
void case_reorder_by_key_passes() {
  lists::KeyCheck before =
      lists::checkKeys({k("a"), k("b"), k("c")}, /*isDev=*/false);
  lists::KeyCheck after =
      lists::checkKeys({k("c"), k("a"), k("b")}, /*isDev=*/false);
  check(before.ok && after.ok, "reorder_by_key_passes");
}

}  // namespace

int main() {
  case_unique_keys_pass_release();
  case_unique_keys_pass_dev();
  case_empty_list_passes();
  case_duplicate_release_reports_keys();
  case_single_duplicate_pair_release();
  case_unstable_key_flagged_release();
  case_all_unstable_flagged();
  case_dev_duplicate_aborts_loudly();
  case_dev_unstable_aborts_loudly();
  case_reorder_by_key_passes();
  if (g_failures == 0) {
    std::printf("ALL TESTS PASSED\n");
  } else {
    std::printf("%d TEST(S) FAILED\n", g_failures);
  }
  return g_failures == 0 ? 0 : 1;
}

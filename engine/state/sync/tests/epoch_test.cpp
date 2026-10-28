// Batch 1 (FR-STATE-005) contract tests — T3 epoch namespacing.
// Spec refs: FR-STATE-005, AC-STATE-024 (TC-STATE-024).
// Hermetic: C++17 + committed engine/state headers only; no env/config/network.
// Binds ONLY to the T3 `Interface:` line; never to implementation.
//
// Structural ASSUMPTIONS (see unit-tests/T3.md):
//   A-EP1: namespace muse::state; Epoch is an aggregate { account, generation }.
//   A-EP2: EpochStore default-constructible; current(nullptr)/advance(nullptr, e)
//          return Status::kNotFound, never crash.
#include <cstdint>
#include <cstdio>

#include "muse/state/atom.hpp"
#include "muse/state/epoch.hpp"

namespace {

using muse::state::Epoch;
using muse::state::EpochStore;
using muse::state::Status;

static bool check_current_unknown_account_returns_not_found() {
  EpochStore s;
  auto r = s.current("ghost:account");
  return !r.ok() && r.status() == Status::kNotFound;
}

static bool check_advance_then_current_roundtrip() {
  EpochStore s;
  Epoch e{7, 3};
  if (s.advance("user:a", e) != Status::kOk) return false;
  auto cur = s.current("user:a");
  if (!cur.ok()) return false;
  return cur.value().account == 7 && cur.value().generation == 3;
}

static bool check_supersedes_semantics() {
  Epoch older{1, 1};
  Epoch newer{1, 2};
  if (!newer.supersedes(older)) return false;
  if (older.supersedes(newer)) return false;
  if (older.supersedes(older)) return false;  // equal never supersedes
  Epoch otherAccount{2, 1};
  if (otherAccount.supersedes(older)) return false;  // ASSUMPTION: same-account
  return true;
}

static bool check_stale_advance_rejected_and_state_unchanged() {
  EpochStore s;
  Epoch newer{1, 5};
  if (s.advance("user:a", newer) != Status::kOk) return false;
  Epoch stale{1, 2};
  if (stale.supersedes(newer)) return false;
  Status st = s.advance("user:a", stale);
  if (st == Status::kOk) return false;  // must be denied (kNotFound-class)
  auto cur = s.current("user:a");
  if (!cur.ok()) return false;
  // Bit-identical: newer state untouched by the stale attempt.
  return cur.value().account == 1 && cur.value().generation == 5;
}

static bool check_epochs_namespaced_per_account() {
  EpochStore s;
  if (s.advance("user:a", Epoch{1, 9}) != Status::kOk) return false;
  if (s.advance("user:b", Epoch{2, 1}) != Status::kOk) return false;
  auto a = s.current("user:a");
  auto b = s.current("user:b");
  if (!a.ok() || !b.ok()) return false;
  return a.value().generation == 9 && b.value().generation == 1;
}

static bool check_null_account_is_rejected() {
  EpochStore s;
  auto r = s.current(nullptr);
  if (r.ok()) return false;
  return s.advance(nullptr, Epoch{1, 1}) != Status::kOk;
}

}  // namespace

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_current_unknown_account_returns_not_found);
  RUN_CASE(check_advance_then_current_roundtrip);
  RUN_CASE(check_supersedes_semantics);
  RUN_CASE(check_stale_advance_rejected_and_state_unchanged);
  RUN_CASE(check_epochs_namespaced_per_account);
  RUN_CASE(check_null_account_is_rejected);
  if (failures > 0) {
    std::printf("epoch_test: %d failing\n", failures);
    return 1;
  }
  std::printf("epoch_test: all passing\n");
  return 0;
}

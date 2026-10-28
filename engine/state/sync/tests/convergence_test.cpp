// Batch 2 (FR-STATE-005) contract tests — T5 convergence + delta sync.
// Spec refs: FR-STATE-005, AC-STATE-025 (TC-STATE-025).
// Hermetic: C++17 + committed engine/state headers only; fixed in-test data
// (no RNG); no env/config/network. Convergence uses the committed T2 merge
// (assumption A1 policies) to fold mutation sets; delta behavior binds ONLY
// to the T5 `Interface:` line.
//
// Structural ASSUMPTIONS (see unit-tests/T5.md):
//   A-DL1: namespace muse::state.
//   A-DL2: SnapshotState is an aggregate wrapping an ordered field vector,
//          e.g. `{ std::vector<TypedValue> fields; }`, equality-comparable
//          via operator==, default- and vector-constructible.
//   A-DL3: Delta is opaque with `std::size_t fieldCount() const` = number of
//          transferred (changed) fields; diff/apply are free functions in
//          muse::state; apply(nullptr, d) returns Status::kNotFound, never
//          crashes.
#include <cstddef>
#include <cstdio>
#include <vector>

#include "muse/state/atom.hpp"
#include "muse/state/crdt.hpp"
#include "muse/state/delta.hpp"

namespace {

using muse::state::CrdtPolicy;
using muse::state::Delta;
using muse::state::FieldState;
using muse::state::SnapshotState;
using muse::state::Status;
using muse::state::TypedValue;

static SnapshotState make_range(int base, std::size_t n) {
  std::vector<TypedValue> v;
  v.reserve(n);
  for (std::size_t i = 0; i < n; ++i) v.emplace_back(base + static_cast<int>(i));
  return SnapshotState{v};
}

static bool check_diff_identical_snapshots_has_zero_fields() {
  SnapshotState base = make_range(0, 8);
  SnapshotState next = make_range(0, 8);
  auto d = muse::state::diff(base, next);
  if (!d.ok()) return false;
  return d.value().fieldCount() == 0;
}

static bool check_diff_single_field_change_counts_one() {
  SnapshotState base = make_range(0, 8);
  SnapshotState next = make_range(0, 8);
  next.fields[3] = TypedValue(99);
  auto d = muse::state::diff(base, next);
  if (!d.ok()) return false;
  return d.value().fieldCount() == 1;
}

static bool check_apply_reproduces_next_exactly() {
  SnapshotState base = make_range(0, 16);
  SnapshotState next = make_range(0, 16);
  next.fields[0] = TypedValue(-1);
  next.fields[7] = TypedValue(-2);
  next.fields[15] = TypedValue(-3);
  auto d = muse::state::diff(base, next);
  if (!d.ok()) return false;
  if (d.value().fieldCount() != 3) return false;
  SnapshotState replica = base;
  if (muse::state::apply(&replica, d.value()) != Status::kOk) return false;
  return replica == next;
}

static bool check_apply_empty_delta_is_noop() {
  SnapshotState base = make_range(5, 8);
  SnapshotState replica = base;
  auto d = muse::state::diff(base, base);
  if (!d.ok() || d.value().fieldCount() != 0) return false;
  if (muse::state::apply(&replica, d.value()) != Status::kOk) return false;
  return replica == base;
}

static bool check_apply_null_snapshot_is_rejected() {
  SnapshotState base = make_range(0, 4);
  SnapshotState next = make_range(1, 4);
  auto d = muse::state::diff(base, next);
  if (!d.ok()) return false;
  return muse::state::apply(nullptr, d.value()) != Status::kOk;
}

static bool check_thousand_item_update_transfers_only_changed_fields() {
  // AC-STATE-025: a 1000-item list update transfers only changed fields.
  SnapshotState base = make_range(0, 1000);
  SnapshotState next = make_range(0, 1000);
  const std::size_t touched[] = {3, 17, 256, 511, 777, 888, 999};
  for (std::size_t i : touched) next.fields[i] = TypedValue(-static_cast<int>(i));
  auto d = muse::state::diff(base, next);
  if (!d.ok()) return false;
  if (d.value().fieldCount() != sizeof(touched) / sizeof(touched[0])) {
    return false;
  }
  SnapshotState replica = base;
  if (muse::state::apply(&replica, d.value()) != Status::kOk) return false;
  return replica == next;
}

static bool check_replicas_fed_same_mutations_converge_byte_identical() {
  // AC-STATE-025: two replicas fed the same mutation set in different
  // orders reach byte-identical state (folded here through the T2 LWW
  // merge, assumption A1 default policy, over per-field versions).
  const int values[] = {10, 20, 30, 40};
  const int order_a[] = {0, 1, 2, 3};
  const int order_b[] = {3, 1, 0, 2};
  FieldState acc_a{TypedValue(values[order_a[0]]), 1};
  for (int k = 1; k < 4; ++k) {
    auto r = muse::state::merge(CrdtPolicy::kLwwRegister, acc_a,
                                FieldState(TypedValue(values[order_a[k]]),
                                           static_cast<std::uint64_t>(k + 1)));
    if (!r.ok()) return false;
    acc_a = r.value();
  }
  FieldState acc_b{TypedValue(values[order_b[0]]), 4};
  // Versions travel with the mutation, not the arrival order.
  const std::uint64_t ver_b[] = {4, 2, 1, 3};
  for (int k = 1; k < 4; ++k) {
    auto r = muse::state::merge(CrdtPolicy::kLwwRegister, acc_b,
                                FieldState(TypedValue(values[order_b[k]]),
                                           ver_b[static_cast<std::size_t>(k)]));
    if (!r.ok()) return false;
    acc_b = r.value();
  }
  if (!(acc_a == acc_b)) return false;
  SnapshotState replica_a{{acc_a.value()}};
  SnapshotState replica_b{{acc_b.value()}};
  if (!(replica_a == replica_b)) return false;
  // And a delta between the converged replicas is empty.
  auto d = muse::state::diff(replica_a, replica_b);
  return d.ok() && d.value().fieldCount() == 0;
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
  RUN_CASE(check_diff_identical_snapshots_has_zero_fields);
  RUN_CASE(check_diff_single_field_change_counts_one);
  RUN_CASE(check_apply_reproduces_next_exactly);
  RUN_CASE(check_apply_empty_delta_is_noop);
  RUN_CASE(check_apply_null_snapshot_is_rejected);
  RUN_CASE(check_thousand_item_update_transfers_only_changed_fields);
  RUN_CASE(check_replicas_fed_same_mutations_converge_byte_identical);
  if (failures > 0) {
    std::printf("convergence_test: %d failing\n", failures);
    return 1;
  }
  std::printf("convergence_test: all passing\n");
  return 0;
}

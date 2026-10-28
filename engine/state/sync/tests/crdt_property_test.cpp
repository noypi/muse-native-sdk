// Batch 1 (FR-STATE-005) contract tests — T2 CRDT merge engine.
// Spec refs: FR-STATE-005, AC-STATE-022 (TC-STATE-022), assumption A1.
// Hermetic: C++17 + committed engine/state headers only; fixed-seed
// in-test op permutations; no env/config/network.
// Binds ONLY to the T2 `Interface:` line; never to implementation.
//
// Structural ASSUMPTIONS (see unit-tests/T2.md):
//   A-CR1: namespace muse::state.
//   A-CR2: FieldState { TypedValue value; uint64_t version; } constructible
//          as FieldState(TypedValue, uint64_t), equality-comparable, with
//          value() and version() accessors.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "muse/state/atom.hpp"
#include "muse/state/crdt.hpp"

namespace {

using muse::state::CrdtPolicy;
using muse::state::CrdtRegistry;
using muse::state::FieldState;
using muse::state::Status;
using muse::state::TypedValue;

static FieldState fs(int v, std::uint64_t ver) {
  return FieldState(TypedValue(v), ver);
}

static bool check_lww_register_picks_higher_version() {
  auto r = muse::state::merge(CrdtPolicy::kLwwRegister, fs(1, 1), fs(2, 2));
  if (!r.ok() || !(r.value() == fs(2, 2))) return false;
  auto r2 = muse::state::merge(CrdtPolicy::kLwwRegister, fs(2, 2), fs(1, 1));
  return r2.ok() && r2.value() == fs(2, 2);
}

static bool check_merge_is_commutative_per_policy() {
  const CrdtPolicy ps[] = {CrdtPolicy::kLwwRegister, CrdtPolicy::kOrSet,
                           CrdtPolicy::kRgaSequence};
  for (auto p : ps) {
    auto ab = muse::state::merge(p, fs(1, 3), fs(2, 5));
    auto ba = muse::state::merge(p, fs(2, 5), fs(1, 3));
    if (!ab.ok() || !ba.ok() || !(ab.value() == ba.value())) return false;
  }
  return true;
}

static bool check_merge_is_idempotent_per_policy() {
  const CrdtPolicy ps[] = {CrdtPolicy::kLwwRegister, CrdtPolicy::kOrSet,
                           CrdtPolicy::kRgaSequence};
  for (auto p : ps) {
    auto once = muse::state::merge(p, fs(4, 4), fs(9, 9));
    if (!once.ok()) return false;
    auto twice = muse::state::merge(p, once.value(), once.value());
    if (!twice.ok() || !(twice.value() == once.value())) return false;
  }
  return true;
}

static bool check_merge_is_associative_per_policy() {
  const CrdtPolicy ps[] = {CrdtPolicy::kLwwRegister, CrdtPolicy::kOrSet,
                           CrdtPolicy::kRgaSequence};
  for (auto p : ps) {
    FieldState a = fs(1, 1);
    FieldState b = fs(2, 2);
    FieldState c = fs(3, 3);
    auto ab = muse::state::merge(p, a, b);
    auto bc = muse::state::merge(p, b, c);
    if (!ab.ok() || !bc.ok()) return false;
    auto left = muse::state::merge(p, ab.value(), c);
    auto right = muse::state::merge(p, a, bc.value());
    if (!left.ok() || !right.ok() || !(left.value() == right.value())) {
      return false;
    }
  }
  return true;
}

// Property test: fixed-seed permutations of op orders converge (A1 mapping:
// RGA for ordered collections, OR-Set for tag-like sets, LWW default).
static bool check_property_permutation_orders_converge() {
  const CrdtPolicy ps[] = {CrdtPolicy::kLwwRegister, CrdtPolicy::kOrSet,
                           CrdtPolicy::kRgaSequence};
  // Fixed seed set of field states (no RNG: deterministic permutations).
  const std::vector<FieldState> ops = {fs(10, 1), fs(20, 2), fs(30, 3),
                                       fs(40, 4)};
  const int orders[4][4] = {{0, 1, 2, 3}, {3, 2, 1, 0}, {1, 3, 0, 2},
                            {2, 0, 3, 1}};
  for (auto p : ps) {
    FieldState baseline = ops[0];
    for (int k = 1; k < 4; ++k) {
      auto r = muse::state::merge(p, baseline, ops[static_cast<std::size_t>(k)]);
      if (!r.ok()) return false;
      baseline = r.value();
    }
    for (const auto& order : orders) {
      FieldState acc = ops[static_cast<std::size_t>(order[0])];
      for (int k = 1; k < 4; ++k) {
        auto r =
            muse::state::merge(p, acc, ops[static_cast<std::size_t>(order[k])]);
        if (!r.ok()) return false;
        acc = r.value();
      }
      if (!(acc == baseline)) return false;
    }
  }
  return true;
}

static bool check_unregistered_entity_policy_for_returns_not_found() {
  CrdtRegistry reg;
  auto r = reg.policyFor("chat:message:unregistered-xyz");
  return !r.ok() && r.status() == Status::kNotFound;
}

static bool check_register_policy_roundtrip() {
  CrdtRegistry reg;
  if (reg.registerPolicy("chat:message", CrdtPolicy::kRgaSequence) !=
      Status::kOk) {
    return false;
  }
  if (reg.registerPolicy("user:tags", CrdtPolicy::kOrSet) != Status::kOk) {
    return false;
  }
  auto a = reg.policyFor("chat:message");
  auto b = reg.policyFor("user:tags");
  if (!a.ok() || a.value() != CrdtPolicy::kRgaSequence) return false;
  return b.ok() && b.value() == CrdtPolicy::kOrSet;
}

static bool check_register_policy_null_entity_is_rejected() {
  CrdtRegistry reg;
  return reg.registerPolicy(nullptr, CrdtPolicy::kLwwRegister) != Status::kOk;
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
  RUN_CASE(check_lww_register_picks_higher_version);
  RUN_CASE(check_merge_is_commutative_per_policy);
  RUN_CASE(check_merge_is_idempotent_per_policy);
  RUN_CASE(check_merge_is_associative_per_policy);
  RUN_CASE(check_property_permutation_orders_converge);
  RUN_CASE(check_unregistered_entity_policy_for_returns_not_found);
  RUN_CASE(check_register_policy_roundtrip);
  RUN_CASE(check_register_policy_null_entity_is_rejected);
  if (failures > 0) {
    std::printf("crdt_property_test: %d failing\n", failures);
    return 1;
  }
  std::printf("crdt_property_test: all passing\n");
  return 0;
}

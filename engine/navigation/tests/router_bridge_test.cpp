// T4 contract tests — FR-NAV-005 / AC-NAV-011 (TC-NAV-011).
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services. Consults no implementation;
// binds to the T4 `Interface:` line verbatim (see unit-tests/T4.md).
//
// ASSUMPTION (construction): the plan `Interface:` line names no
// constructor. These cases construct the bridge with the registered
// route table, `explicit RouterBridge(std::vector<std::string> routes)`,
// mirroring T1's `NavGraph` convention. Update call sites, not
// expectations, if the coder names it differently.
// ASSUMPTION (method vocabulary): invoke() accepts methods "navigate",
// "push", "pop", "back" with jsonArgs `{"route":"<id>"}` for
// navigate/push and `{}` for pop/back. Unknown route → NAV_ERROR;
// malformed JSON / missing route field → BAD_ARGS.
#include <cstdio>
#include <string>
#include <vector>

#include "navigation/router_bridge.h"

namespace {

int failures = 0;

#define EXPECT_TRUE(cond, name)                       \
  do {                                                \
    if (!(cond)) {                                    \
      std::printf("FAIL %s\n", name);                 \
      ++failures;                                     \
    } else {                                          \
      std::printf("PASS %s\n", name);                 \
    }                                                 \
  } while (0)

const std::vector<std::string> kRoutes = {"home", "details", "settings"};

bool check_unknown_method() {
  RouterBridge b(kRoutes);
  BridgeResult r = b.invoke("teleport", "{}");
  return !r.ok && r.json.empty() && r.error == BridgeError::UNKNOWN_METHOD;
}

bool check_navigate_ok() {
  RouterBridge b(kRoutes);
  BridgeResult r = b.invoke("navigate", "{\"route\":\"details\"}");
  return r.ok && r.error == BridgeError::NONE;
}

bool check_push_ok() {
  RouterBridge b(kRoutes);
  BridgeResult r = b.invoke("push", "{\"route\":\"settings\"}");
  return r.ok && r.error == BridgeError::NONE;
}

bool check_navigate_mutates_graph_observable_via_snapshot() {
  RouterBridge b(kRoutes);
  std::string before = b.snapshotForPersist();
  BridgeResult r = b.invoke("navigate", "{\"route\":\"details\"}");
  if (!r.ok) return false;
  std::string after = b.snapshotForPersist();
  return !before.empty() && !after.empty() && before != after;
}

bool check_back_pops_after_push() {
  RouterBridge b(kRoutes);
  std::string homeSnap = b.snapshotForPersist();
  if (!b.invoke("push", "{\"route\":\"details\"}").ok) return false;
  BridgeResult back = b.invoke("back", "{}");
  if (!back.ok) return false;
  return b.snapshotForPersist() == homeSnap;
}

bool check_pop_alias() {
  RouterBridge b(kRoutes);
  if (!b.invoke("push", "{\"route\":\"details\"}").ok) return false;
  BridgeResult r = b.invoke("pop", "{}");
  return r.ok && r.error == BridgeError::NONE;
}

bool check_bad_args_malformed_json() {
  RouterBridge b(kRoutes);
  BridgeResult r = b.invoke("navigate", "not-json");
  return !r.ok && r.error == BridgeError::BAD_ARGS;
}

bool check_bad_args_missing_route() {
  RouterBridge b(kRoutes);
  BridgeResult r = b.invoke("navigate", "{}");
  return !r.ok && r.error == BridgeError::BAD_ARGS;
}

bool check_nav_error_unknown_route() {
  RouterBridge b(kRoutes);
  BridgeResult r = b.invoke("navigate", "{\"route\":\"no_such_route\"}");
  return !r.ok && r.error == BridgeError::NAV_ERROR;
}

bool check_snapshot_delegates_to_canonical_format() {
  RouterBridge b(kRoutes);
  std::string snap = b.snapshotForPersist();
  return snap.rfind("NAV1;", 0) == 0;
}

bool check_snapshot_restore_round_trip() {
  RouterBridge b(kRoutes);
  if (!b.invoke("push", "{\"route\":\"details\"}").ok) return false;
  std::string snap = b.snapshotForPersist();
  RouterBridge b2(kRoutes);
  if (!b2.restoreFromLifeSave(snap)) return false;
  return b2.snapshotForPersist() == snap;
}

bool check_restore_failure_returns_false_and_resets_home() {
  RouterBridge b(kRoutes);
  if (!b.invoke("push", "{\"route\":\"details\"}").ok) return false;
  bool restored = b.restoreFromLifeSave("CORRUPT-BLOB");
  if (restored) return false;
  RouterBridge fresh(kRoutes);
  return b.snapshotForPersist() == fresh.snapshotForPersist();
}

bool check_empty_method_is_unknown() {
  RouterBridge b(kRoutes);
  BridgeResult r = b.invoke("", "{}");
  return !r.ok && r.error == BridgeError::UNKNOWN_METHOD;
}

// ---- Batch 1 RED pins (GR-08 T1.1): BUG-NAV-001 guard hook (T1) ----
// Spec-derived only (AC-NAV-006, AC-NAV-042; AC-NAV-040 names the
// `guard-denied` denial object; commander ruling pins the C++ code as
// `BridgeError::GUARD_DENIED`). Consults no implementation.
// ASSUMPTION (hook signature): `setGuardHook(GuardFn)` with
// `GuardFn = std::function<bool(const std::string& routeId)>` from
// `navigation/snapshot.h`; a null/unset hook allows every route,
// preserving the `router_bridge.h:38-40` pre-gate behavior.

bool check_t1_denying_guard_denies_navigate_with_guard_denied() {
  RouterBridge b(kRoutes);
  std::string before = b.snapshotForPersist();
  b.setGuardHook([](const std::string&) { return false; });
  BridgeResult r = b.invoke("navigate", "{\"route\":\"details\"}");
  return !r.ok && r.error == BridgeError::GUARD_DENIED &&
         b.snapshotForPersist() == before;
}

bool check_t1_denying_guard_denies_push_with_guard_denied() {
  RouterBridge b(kRoutes);
  std::string before = b.snapshotForPersist();
  b.setGuardHook([](const std::string&) { return false; });
  BridgeResult r = b.invoke("push", "{\"route\":\"settings\"}");
  return !r.ok && r.error == BridgeError::GUARD_DENIED &&
         b.snapshotForPersist() == before;
}

bool check_t1_allowing_guard_commits_navigate() {
  RouterBridge b(kRoutes);
  b.setGuardHook([](const std::string&) { return true; });
  BridgeResult r = b.invoke("navigate", "{\"route\":\"details\"}");
  return r.ok && r.error == BridgeError::NONE;
}

bool check_t1_unset_guard_still_allows_navigate() {
  // Pre-gate behavior preserved: no hook installed, known route commits.
  RouterBridge b(kRoutes);
  std::string before = b.snapshotForPersist();
  BridgeResult r = b.invoke("navigate", "{\"route\":\"details\"}");
  return r.ok && r.error == BridgeError::NONE &&
         b.snapshotForPersist() != before;
}

// ---- Batch 1 RED pins (GR-08 T1.1): BUG-NAV-002 descriptors (T2) ----
// Spec-derived only (AC-NAV-017, AC-NAV-042). Every manifest route must
// validate against its codegen'd descriptor; unknown routes keep the
// FR-NAV-004 home-degrade (empty-params fallback).
// ASSUMPTION (manifest route): the plan names no non-`profile` route.
// These cases propose route "lens" with one required string param "id",
// mirroring the `router_facade_gate_test.cpp` fixture. Update the route
// name/params call sites — not the deny/commit expectations — if the
// checked-in manifest names it differently.
const std::vector<std::string> kLensRoutes = {"home", "lens"};

bool check_t2_missing_required_param_denied_bad_args() {
  RouterBridge b(kLensRoutes);
  std::string before = b.snapshotForPersist();
  BridgeResult r = b.invoke("navigate", "{\"route\":\"lens\"}");
  return !r.ok && r.error == BridgeError::BAD_ARGS &&
         b.snapshotForPersist() == before;
}

bool check_t2_valid_params_commit_ok() {
  RouterBridge b(kLensRoutes);
  BridgeResult r =
      b.invoke("navigate", "{\"route\":\"lens\",\"params\":{\"id\":\"a\"}}");
  return r.ok && r.error == BridgeError::NONE;
}

bool check_t2_wrong_typed_param_denied_bad_args() {
  // Denial already holds pre-fix (empty-schema UNKNOWN_PARAM); post-fix
  // the same denial must hold via the descriptor (TYPE_MISMATCH). Pinned
  // to stay denying, not as a red pin.
  RouterBridge b(kLensRoutes);
  std::string before = b.snapshotForPersist();
  BridgeResult r =
      b.invoke("navigate", "{\"route\":\"lens\",\"params\":{\"id\":7}}");
  return !r.ok && r.error == BridgeError::BAD_ARGS &&
         b.snapshotForPersist() == before;
}

bool check_t2_profile_valid_params_still_commit() {
  // A2 preservation: `profile` (userId:String required + tab:Int optional)
  // keeps byte-identical behavior through the generalization.
  const std::vector<std::string> routes = {"home", "profile"};
  RouterBridge b(routes);
  BridgeResult r = b.invoke(
      "navigate",
      "{\"route\":\"profile\",\"params\":{\"userId\":\"ada\",\"tab\":2}}");
  return r.ok && r.error == BridgeError::NONE;
}

}  // namespace

int main() {
  EXPECT_TRUE(check_unknown_method(), "unknown method returns UNKNOWN_METHOD with empty json");
  EXPECT_TRUE(check_navigate_ok(), "navigate happy path ok");
  EXPECT_TRUE(check_push_ok(), "push happy path ok");
  EXPECT_TRUE(check_navigate_mutates_graph_observable_via_snapshot(),
              "invoke navigate mutates graph observable via snapshot");
  EXPECT_TRUE(check_back_pops_after_push(), "back pops to home snapshot");
  EXPECT_TRUE(check_pop_alias(), "pop alias ok");
  EXPECT_TRUE(check_bad_args_malformed_json(), "malformed jsonArgs returns BAD_ARGS");
  EXPECT_TRUE(check_bad_args_missing_route(), "missing route returns BAD_ARGS");
  EXPECT_TRUE(check_nav_error_unknown_route(), "unknown route returns NAV_ERROR");
  EXPECT_TRUE(check_snapshot_delegates_to_canonical_format(),
              "snapshot delegates to T1/T6 canonical NAV1 format");
  EXPECT_TRUE(check_snapshot_restore_round_trip(), "snapshot/restore round trip preserves stack");
  EXPECT_TRUE(check_restore_failure_returns_false_and_resets_home(),
              "restore failure returns false and resets home");
  EXPECT_TRUE(check_empty_method_is_unknown(), "empty method returns UNKNOWN_METHOD");
  EXPECT_TRUE(check_t1_denying_guard_denies_navigate_with_guard_denied(),
              "T1 denying guard denies navigate with GUARD_DENIED, graph unchanged");
  EXPECT_TRUE(check_t1_denying_guard_denies_push_with_guard_denied(),
              "T1 denying guard denies push with GUARD_DENIED, graph unchanged");
  EXPECT_TRUE(check_t1_allowing_guard_commits_navigate(),
              "T1 allowing guard commits navigate");
  EXPECT_TRUE(check_t1_unset_guard_still_allows_navigate(),
              "T1 unset guard still allows navigate (pre-gate preserved)");
  EXPECT_TRUE(check_t2_missing_required_param_denied_bad_args(),
              "T2 missing required param denied BAD_ARGS, graph unchanged");
  EXPECT_TRUE(check_t2_valid_params_commit_ok(),
              "T2 valid params commit ok");
  EXPECT_TRUE(check_t2_wrong_typed_param_denied_bad_args(),
              "T2 wrong-typed param denied BAD_ARGS, graph unchanged");
  EXPECT_TRUE(check_t2_profile_valid_params_still_commit(),
              "T2 profile valid params still commit (A2 preserved)");
  std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES %d\n", failures);
  return failures == 0 ? 0 : 1;
}

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
  std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES %d\n", failures);
  return failures == 0 ? 0 : 1;
}

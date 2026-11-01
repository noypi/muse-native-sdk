// Batch 2 tests — FR-NAV-012 / AC-NAV-035 (TC-NAV-035).
// Proves arbitrary routing lands behind the existing Surface seam unchanged:
// (a) seam interface unchanged — only RouterBridge invoke/snapshot/restore
// entry points, no new product entry; (b) presentation order unchanged —
// commit ordering of a nested/deep arbitrary route matches the pre-existing
// parent-chain push order; (c) shell-chrome-only paging rules untouched —
// no tap/surface-gesture path added in engine/navigation.
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services. Derived from specs/ACs above,
// never from the implementation.
//
// ASSUMPTION (construction): cases construct `explicit RouterBridge(
// std::vector<std::string> routes)` mirroring router_bridge_test.cpp.
// Update call sites, not expectations, if the coder names it differently.
// ASSUMPTION (ordering observable): the canonical snapshot blob lists
// committed routes in push order, so parent-chain order is asserted via
// increasing substring positions. If the canonical format ever stops
// embedding route ids in order, this assertion needs a new observable.
#include <cstdio>
#include <string>
#include <vector>

#include "navigation/nested_graph.h"
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

const std::vector<std::string> kRoutes = {
    "home", "item1", "item2", "item3", "details", "settings"};

bool contains(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}

// Ordered positions: every needle appears strictly after the previous one.
bool containsInOrder(const std::string& hay,
                     const std::vector<std::string>& needles) {
  std::string::size_type pos = 0;
  for (const auto& n : needles) {
    std::string::size_type found = hay.find(n, pos);
    if (found == std::string::npos) return false;
    pos = found + n.size();
  }
  return true;
}

NestedManifest makeTabManifest() {
  NestedManifest m;
  GraphDecl root;
  root.graphId = "root";
  root.parent = "";
  root.routes = {"home"};
  GraphDecl tab2;
  tab2.graphId = "tab-2";
  tab2.parent = "root";
  tab2.routes = {"item1", "item2", "item3"};
  m.graphs = {root, tab2};
  return m;
}

// (a) The seam entry points keep working: navigate commits, and the commit
// is observable through the unchanged snapshot/restore pair.
bool check_seam_entries_still_commit_and_persist() {
  RouterBridge b(kRoutes);
  if (!b.invoke("navigate", "{\"route\":\"details\"}").ok) return false;
  std::string snap = b.snapshotForPersist();
  if (snap.rfind("NAV1;", 0) != 0) return false;
  if (!contains(snap, "details")) return false;
  RouterBridge b2(kRoutes);
  if (!b2.restoreFromLifeSave(snap)) return false;
  return b2.snapshotForPersist() == snap;
}

// (a)+(c) No new product entry point: forward-only / gesture-flavored
// methods are rejected as unknown instead of navigating.
bool check_no_advance_page_entry() {
  RouterBridge b(kRoutes);
  std::string before = b.snapshotForPersist();
  BridgeResult r = b.invoke("advancePage", "{\"route\":\"details\"}");
  if (r.ok || r.error != BridgeError::UNKNOWN_METHOD) return false;
  return b.snapshotForPersist() == before;
}

bool check_no_tap_entry() {
  RouterBridge b(kRoutes);
  std::string before = b.snapshotForPersist();
  BridgeResult r = b.invoke("tap", "{\"route\":\"details\"}");
  if (r.ok || r.error != BridgeError::UNKNOWN_METHOD) return false;
  return b.snapshotForPersist() == before;
}

bool check_no_gesture_entry() {
  RouterBridge b(kRoutes);
  std::string before = b.snapshotForPersist();
  BridgeResult r = b.invoke("swipe", "{\"route\":\"details\"}");
  if (r.ok || r.error != BridgeError::UNKNOWN_METHOD) return false;
  BridgeResult r2 = b.invoke("gesture", "{\"route\":\"details\"}");
  if (r2.ok || r2.error != BridgeError::UNKNOWN_METHOD) return false;
  return b.snapshotForPersist() == before;
}

// (b) Commit ordering of a nested/deep arbitrary route matches the
// pre-existing parent-chain push order: resolve the nested leaf, replay
// the chain through the seam, and require identical order.
bool check_nested_commit_matches_parent_chain_order() {
  NestedResult nested =
      resolveNested(makeTabManifest(), "app", {"tab-2", "item3"});
  if (!nested.ok) return false;
  // Chain after home: item1 -> item2 -> item3.
  RouterBridge b({"home", "item1", "item2", "item3"});
  for (const auto& route : nested.stack) {
    if (route == "home") continue;
    BridgeResult r = b.invoke("navigate", "{\"route\":\"" + route + "\"}");
    if (!r.ok) return false;
  }
  std::string snap = b.snapshotForPersist();
  return containsInOrder(snap, {"home", "item1", "item2", "item3"});
}

// (b) A deep arbitrary route lands in one invoke with the same canonical
// envelope — the seam, not a new path, carries it.
bool check_deep_arbitrary_lands_behind_same_seam() {
  RouterBridge b(kRoutes);
  BridgeResult r = b.invoke("navigate", "{\"route\":\"settings\"}");
  if (!r.ok || r.error != BridgeError::NONE) return false;
  std::string snap = b.snapshotForPersist();
  return snap.rfind("NAV1;", 0) == 0 && contains(snap, "settings");
}

// (b) Presentation order preserved: back pops reveal the previous commit,
// so paging chrome walks the same stack the seam committed.
bool check_back_walks_commits_in_reverse() {
  RouterBridge b(kRoutes);
  std::string homeSnap = b.snapshotForPersist();
  if (!b.invoke("navigate", "{\"route\":\"item1\"}").ok) return false;
  std::string firstSnap = b.snapshotForPersist();
  if (!b.invoke("navigate", "{\"route\":\"item2\"}").ok) return false;
  if (!b.invoke("back", "{}").ok) return false;
  if (b.snapshotForPersist() != firstSnap) return false;
  if (!b.invoke("back", "{}").ok) return false;
  return b.snapshotForPersist() == homeSnap;
}

}  // namespace

int main() {
  EXPECT_TRUE(check_seam_entries_still_commit_and_persist(),
              "seam invoke/snapshot/restore entries still commit and persist");
  EXPECT_TRUE(check_no_advance_page_entry(),
              "no advancePage product entry (UNKNOWN_METHOD, no state change)");
  EXPECT_TRUE(check_no_tap_entry(),
              "no tap product entry (UNKNOWN_METHOD, no state change)");
  EXPECT_TRUE(check_no_gesture_entry(),
              "no swipe/gesture product entry (UNKNOWN_METHOD, no state change)");
  EXPECT_TRUE(check_nested_commit_matches_parent_chain_order(),
              "nested arbitrary commit matches parent-chain push order");
  EXPECT_TRUE(check_deep_arbitrary_lands_behind_same_seam(),
              "deep arbitrary route lands behind same seam envelope");
  EXPECT_TRUE(check_back_walks_commits_in_reverse(),
              "back walks commits in reverse presentation order");
  std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES %d\n", failures);
  return failures == 0 ? 0 : 1;
}

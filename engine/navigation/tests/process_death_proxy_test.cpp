// T6 contract tests — FR-NAV-009 / AC-NAV-028 (TC-NAV-028 proxy) +
// FR-NAV-001 / AC-NAV-003 (kill-restore round trip).
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services. Binds to the T6 `Interface:`
// lines verbatim (see unit-tests/T6.md):
//   existing `std::string RouterBridge::snapshotForPersist() const;` +
//   `bool RouterBridge::restoreFromLifeSave(const std::string& blob);` —
//   unchanged signatures; `restoreFromLifeSave` routes through T5
//   `commitRestore` (validate+guard), returns false only on
//   CORRUPT/VALIDATION_REJECTED/GUARD_DENIED (home-degrade returns true).
//   Proxy: `int simulateProcessDeath(RouterBridge& b, size_t maxBytes);`
//   in the test — serialize → destroy graph → fresh bridge → restore;
//   0 = full round-trip equality. No exceptions.
// Depends on: T2 (content sections), T5 (commit gate).
//
// Device-lab proxy note (external TC-NAV-028, `device-lab/process-death`):
// this file stands in for the on-device steps, which remain EXTERNAL:
//   1. Drive the app to a destination with nested stacks + opted-in
//      scroll/focus (selection), background it.
//   2. Kill via "Don't keep activities" (or `am kill`), relaunch.
//   3. LIFE-SAVE envelope hands the persist blob to `restoreFromLifeSave`.
//   4. Compare: destination, nested stacks, scroll+focus with selection;
//      a v1 snapshot must migrate (AC-NAV-028 migration sentence).
// The proxy below performs steps 3–4 host-side: it serializes the live
// bridge, destroys the graph (fresh bridge = dead process), restores,
// and compares snapshots.
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "navigation/graph.h"
#include "navigation/router_bridge.h"
#include "navigation/snapshot.h"
#include "navigation/state_schema.h"

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

std::string homeSnapshot() {
  RouterBridge fresh(kRoutes);
  return fresh.snapshotForPersist();
}

// Host-side device-lab proxy: serialize the live bridge, destroy the graph
// (a fresh bridge stands in for the relaunched process), restore the blob,
// and report 0 iff the restore commits and the snapshot is bit-identical.
// `maxBytes` is the persist budget, enforced via T3 `truncateSnapshot`
// applied by the caller side of this proxy (plan T6 note): the blob is
// truncated to budget before the kill, so an oversize blob exercises the
// T3 drop-priority path instead of round-tripping unbounded.
int simulateProcessDeath(RouterBridge& b, size_t maxBytes) {
  std::string blob;
  try {
    blob = b.snapshotForPersist();
  } catch (...) {
    return 1;
  }
  if (blob.empty()) return 2;
  try {
    SnapshotResult t = truncateSnapshot(blob, maxBytes, TelemetrySink{});
    if (!t.ok) return 9;
    blob = t.blob;
  } catch (...) {
    return 1;
  }
  RouterBridge relaunched(kRoutes);
  bool ok = false;
  try {
    ok = relaunched.restoreFromLifeSave(blob);
  } catch (...) {
    return 3;
  }
  if (!ok) return 4;
  bool equal = false;
  try {
    equal = (relaunched.snapshotForPersist() == blob);
  } catch (...) {
    return 5;
  }
  if (!equal) return 6;
  // Commit back onto the passed bridge so callers observe restored state.
  try {
    if (!b.restoreFromLifeSave(blob)) return 7;
  } catch (...) {
    return 8;
  }
  return 0;
}

bool check_proxy_restores_same_destination() {
  RouterBridge b(kRoutes);
  if (!b.invoke("push", "{\"route\":\"details\"}").ok) return false;
  return simulateProcessDeath(b, 64 * 1024) == 0;
}

bool check_proxy_restores_stack_depth() {
  RouterBridge b(kRoutes);
  if (!b.invoke("push", "{\"route\":\"details\"}").ok) return false;
  if (!b.invoke("push", "{\"route\":\"settings\"}").ok) return false;
  std::string before = b.snapshotForPersist();
  if (simulateProcessDeath(b, 64 * 1024) != 0) return false;
  return b.snapshotForPersist() == before;
}

bool check_proxy_scroll_focus_with_selection_survive() {
  // Content blob built through the T2 path: destination opts in with
  // scroll offset + focus field carrying a selection range.
  NavGraph g(kRoutes);
  g.apply(NavOp{NavOpType::Push, "details", 0});
  ScrollProvider scroll = [](const std::string& routeId) -> std::string {
    if (routeId == "details") return "120";
    return "";
  };
  FocusProvider focus = [](const std::string& routeId) -> std::string {
    if (routeId == "details") return "field=comment,sel=3-7";
    return "";
  };
  SnapshotResult cap = captureSnapshotWithContent(g, scroll, focus);
  if (!cap.ok) return false;
  RouterBridge b(kRoutes);
  bool ok = false;
  try {
    ok = b.restoreFromLifeSave(cap.blob);
  } catch (...) {
    return false;
  }
  if (!ok) return false;
  // Structural state restored; content section (scroll + selection) rode
  // the blob instead of being stripped or corrupting the commit. Install
  // matching providers on the relaunched bridge (a fresh process re-binds
  // its lists/text hooks on start) and assert the re-persisted blob still
  // carries the scroll offset and the focus selection.
  try {
    b.setScrollProvider(scroll);
    b.setFocusProvider(focus);
  } catch (...) {
    return false;
  }
  std::string snap;
  try {
    snap = b.snapshotForPersist();
  } catch (...) {
    return false;
  }
  if (snap.empty() || snap.rfind("NAV1;", 0) != 0) return false;
  if (snap.find("120") == std::string::npos) return false;
  if (snap.find("field=comment,sel=3-7") == std::string::npos) return false;
  NavGraph expect(kRoutes);
  expect.apply(NavOp{NavOpType::Push, "details", 0});
  std::string structural = cap.blob;
  std::string::size_type pos = structural.find(";content=");
  if (pos != std::string::npos) structural.erase(pos);
  std::string repersistStructural = snap;
  pos = repersistStructural.find(";content=");
  if (pos != std::string::npos) repersistStructural.erase(pos);
  return repersistStructural == structural && expect.serialize() == structural;
}

bool check_proxy_v1_snapshot_migrates() {
  NavGraph g(kRoutes);
  g.apply(NavOp{NavOpType::Push, "details", 0});
  NavSnapshot s;
  s.version = 1;
  s.graphBlob = g.serialize();
  EncodeResult e = encodeState(s);
  std::string v1blob = e.ok ? e.blob : g.serialize();
  RouterBridge b(kRoutes);
  bool ok = false;
  try {
    ok = b.restoreFromLifeSave(v1blob);
  } catch (...) {
    return false;
  }
  if (!ok) return false;
  NavGraph expect(kRoutes);
  expect.apply(NavOp{NavOpType::Push, "details", 0});
  return b.snapshotForPersist() == expect.serialize();
}

bool check_proxy_corrupt_returns_false() {
  RouterBridge b(kRoutes);
  if (!b.invoke("push", "{\"route\":\"details\"}").ok) return false;
  bool ok = true;
  try {
    ok = b.restoreFromLifeSave("CORRUPT-BLOB");
  } catch (...) {
    return false;
  }
  return !ok;
}

bool check_proxy_unknown_route_degrades_home_true() {
  RouterBridge b(kRoutes);
  if (!b.invoke("push", "{\"route\":\"details\"}").ok) return false;
  std::string blob = b.snapshotForPersist();
  std::string::size_type pos = blob.find("details");
  if (pos == std::string::npos) return false;
  blob.replace(pos, 7, "removed_x");
  bool ok = false;
  try {
    ok = b.restoreFromLifeSave(blob);
  } catch (...) {
    return false;
  }
  // Home-degrade (FR-NAV-004 AC-NAV-007) commits home and reports true;
  // only CORRUPT/VALIDATION_REJECTED/GUARD_DENIED report false.
  return ok && b.snapshotForPersist() == homeSnapshot();
}

bool check_proxy_budget_truncates_then_restores() {
  // T3 budget path applied caller-side: truncating a content blob to a
  // tight budget fits the budget, raises the truncated flag, and the
  // truncated blob still restores.
  NavGraph g(kRoutes);
  g.apply(NavOp{NavOpType::Push, "details", 0});
  ScrollProvider scroll = [](const std::string& routeId) -> std::string {
    if (routeId == "details") return "120";
    return "";
  };
  FocusProvider focus = [](const std::string& routeId) -> std::string {
    if (routeId == "details") return "field=comment,sel=3-7";
    return "";
  };
  SnapshotResult cap = captureSnapshotWithContent(g, scroll, focus);
  if (!cap.ok) return false;
  std::string structural = cap.blob;
  std::string::size_type pos = structural.find(";content=");
  if (pos != std::string::npos) structural.erase(pos);
  size_t maxBytes = structural.size();
  SnapshotResult t;
  try {
    t = truncateSnapshot(cap.blob, maxBytes, TelemetrySink{});
  } catch (...) {
    return false;
  }
  if (!t.ok || !t.truncated) return false;
  if (t.blob.size() > maxBytes) return false;
  RouterBridge b(kRoutes);
  bool ok = false;
  try {
    ok = b.restoreFromLifeSave(t.blob);
  } catch (...) {
    return false;
  }
  return ok;
}

bool check_proxy_never_throws_no_crash() {
  bool noThrow = true;
  try {
    RouterBridge b(kRoutes);
    if (!b.invoke("push", "{\"route\":\"details\"}").ok) return false;
    if (simulateProcessDeath(b, 64 * 1024) != 0) return false;
    RouterBridge c(kRoutes);
    c.restoreFromLifeSave("");
    c.restoreFromLifeSave("NAV1;v=1;sel=99;tabs=zzz;modals=;drawer=9");
  } catch (...) {
    noThrow = false;
  }
  return noThrow;
}

}  // namespace

int main() {
  EXPECT_TRUE(check_proxy_restores_same_destination(),
              "proxy_restores_same_destination");
  EXPECT_TRUE(check_proxy_restores_stack_depth(),
              "proxy_restores_stack_depth");
  EXPECT_TRUE(check_proxy_scroll_focus_with_selection_survive(),
              "proxy_scroll_focus_with_selection_survive");
  EXPECT_TRUE(check_proxy_v1_snapshot_migrates(),
              "proxy_v1_snapshot_migrates");
  EXPECT_TRUE(check_proxy_corrupt_returns_false(),
              "proxy_corrupt_returns_false");
  EXPECT_TRUE(check_proxy_unknown_route_degrades_home_true(),
              "proxy_unknown_route_degrades_home_true");
  EXPECT_TRUE(check_proxy_budget_truncates_then_restores(),
              "proxy_budget_truncates_then_restores");
  EXPECT_TRUE(check_proxy_never_throws_no_crash(),
              "proxy_never_throws_no_crash");
  if (failures > 0) {
    std::printf("process_death_proxy_test: %d failing\n", failures);
    return 1;
  }
  std::printf("process_death_proxy_test: all passing\n");
  return 0;
}

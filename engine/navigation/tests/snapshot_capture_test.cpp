// T1 contract tests — FR-NAV-009 / AC-NAV-025 (TC-NAV-025).
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services. Binds to the T1 `Interface:`
// line verbatim (see unit-tests/T1.md):
//   `SnapshotResult captureSnapshot(const NavGraph& graph);`
// returns `{ok, blob, error}` where `error ∈ {NONE, CORRUPT}`; `none`
// blob on `!ok`. No exceptions.
// Structural round-trip reuses the existing `NavGraph::restore` /
// `serialize` canonical format (`NAV1;v=1;...`); FR-NAV-004 decode/home
// degrade is reused, not redefined here.
#include <cstdio>
#include <string>
#include <vector>

#include "navigation/graph.h"
#include "navigation/snapshot.h"

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

// TC-NAV-025 graph: 2 tabs (tab0: home->details, tab1: home->settings)
// + 1 modal + drawer open.
NavGraph makeFullGraph() {
  NavGraph g(kRoutes);
  g.apply(NavOp{NavOpType::Push, "details", 0});       // tab0: home,details
  g.apply(NavOp{NavOpType::SwitchTab, "", 1});        // -> tab1
  g.apply(NavOp{NavOpType::Push, "settings", 1});      // tab1: home,settings
  g.apply(NavOp{NavOpType::PushModal, "details", 0});  // 1 modal
  g.apply(NavOp{NavOpType::OpenDrawer, "", 0});        // drawer open
  return g;
}

bool check_capture_ok_and_blob_nonempty() {
  NavGraph g = makeFullGraph();
  SnapshotResult r = captureSnapshot(g);
  return r.ok && r.error == SnapshotError::NONE && !r.blob.empty();
}

bool check_full_graph_round_trips() {
  NavGraph g = makeFullGraph();
  SnapshotResult r = captureSnapshot(g);
  if (!r.ok) return false;
  NavGraph h(kRoutes);
  NavResult rr = h.restore(r.blob);
  return rr.ok && rr.error == NavError::NONE &&
         h.serialize() == g.serialize();
}

bool check_blob_is_versioned() {
  NavGraph g = makeFullGraph();
  SnapshotResult r = captureSnapshot(g);
  if (!r.ok) return false;
  return r.blob.rfind("NAV1;v=1;", 0) == 0;
}

bool check_selected_tab_preserved() {
  NavGraph g = makeFullGraph();  // selected tab is 1
  SnapshotResult r = captureSnapshot(g);
  if (!r.ok) return false;
  NavGraph h(kRoutes);
  if (!h.restore(r.blob).ok) return false;
  // Switching away and back must not be needed: sel=1 survives.
  return h.serialize().find("sel=1;") != std::string::npos;
}

bool check_drawer_and_modal_preserved() {
  NavGraph g = makeFullGraph();
  SnapshotResult r = captureSnapshot(g);
  if (!r.ok) return false;
  NavGraph h(kRoutes);
  if (!h.restore(r.blob).ok) return false;
  const std::string s = h.serialize();
  return s.find("modals=details") != std::string::npos &&
         s.find("drawer=1") != std::string::npos;
}

bool check_empty_modals_closed_drawer_round_trips() {
  NavGraph g(kRoutes);  // fresh: no modals, drawer closed
  SnapshotResult r = captureSnapshot(g);
  if (!r.ok) return false;
  NavGraph h(kRoutes);
  if (!h.restore(r.blob).ok) return false;
  return h.serialize() == g.serialize();
}

bool check_tampered_blob_does_not_restore() {
  NavGraph g = makeFullGraph();
  SnapshotResult r = captureSnapshot(g);
  if (!r.ok || r.blob.size() < 8) return false;
  std::string bad = r.blob;
  bad.replace(0, 4, "XXXX");
  NavGraph h(kRoutes);
  NavResult rr = h.restore(bad);
  return !rr.ok;
}

bool check_capture_never_throws() {
  NavGraph g = makeFullGraph();
  bool noThrow = true;
  SnapshotResult r{false, "", SnapshotError::CORRUPT};
  try {
    r = captureSnapshot(g);
  } catch (...) {
    noThrow = false;
  }
  return noThrow && r.ok;
}

bool check_error_shape_only_none_or_corrupt() {
  NavGraph g = makeFullGraph();
  SnapshotResult r = captureSnapshot(g);
  return r.ok ? (r.error == SnapshotError::NONE)
              : (r.error == SnapshotError::CORRUPT && r.blob.empty());
}

}  // namespace

int main() {
  EXPECT_TRUE(check_capture_ok_and_blob_nonempty(),
              "capture_ok_and_blob_nonempty");
  EXPECT_TRUE(check_full_graph_round_trips(), "full_graph_round_trips");
  EXPECT_TRUE(check_blob_is_versioned(), "blob_is_versioned");
  EXPECT_TRUE(check_selected_tab_preserved(), "selected_tab_preserved");
  EXPECT_TRUE(check_drawer_and_modal_preserved(), "drawer_and_modal_preserved");
  EXPECT_TRUE(check_empty_modals_closed_drawer_round_trips(),
              "empty_modals_closed_drawer_round_trips");
  EXPECT_TRUE(check_tampered_blob_does_not_restore(),
              "tampered_blob_does_not_restore");
  EXPECT_TRUE(check_capture_never_throws(), "capture_never_throws");
  EXPECT_TRUE(check_error_shape_only_none_or_corrupt(),
              "error_shape_only_none_or_corrupt");
  if (failures > 0) {
    std::printf("snapshot_capture_test: %d failing\n", failures);
    return 1;
  }
  std::printf("snapshot_capture_test: all passing\n");
  return 0;
}

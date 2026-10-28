// T2 contract tests — FR-NAV-009 / AC-NAV-026 (TC-NAV-026).
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services. Binds to the T2 `Interface:`
// line verbatim (see unit-tests/T2.md):
//   `SnapshotResult captureSnapshotWithContent(const NavGraph& graph,
//     ScrollProvider scroll, FocusProvider focus);`
//   `using ScrollProvider = std::string(const std::string& routeId)`
//   (empty = opted out),
//   `using FocusProvider = std::string(const std::string& routeId)`
//   (empty = opted out); error shape same as T1 plus `TRUNCATED` flag
// never set here.
// Depends on T1 (structural blob + versioned format); content is an
// opt-in per-destination extension carried in the same blob.
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

NavGraph makeGraph() {
  NavGraph g(kRoutes);
  g.apply(NavOp{NavOpType::Push, "details", 0});
  return g;
}

ScrollProvider kScroll = [](const std::string& routeId) -> std::string {
  if (routeId == "details") return "120";
  return "";
};

FocusProvider kFocus = [](const std::string& routeId) -> std::string {
  if (routeId == "details") return "field=comment,sel=3-7";
  return "";
};

ScrollProvider kOptOut = [](const std::string&) -> std::string { return ""; };
FocusProvider kFocusOut = [](const std::string&) -> std::string { return ""; };

bool check_opted_in_content_captured() {
  NavGraph g = makeGraph();
  SnapshotResult r = captureSnapshotWithContent(g, kScroll, kFocus);
  if (!r.ok) return false;
  return r.blob.find("120") != std::string::npos &&
         r.blob.find("field=comment,sel=3-7") != std::string::npos;
}

bool check_opted_in_content_round_trips_structurally() {
  NavGraph g = makeGraph();
  SnapshotResult r = captureSnapshotWithContent(g, kScroll, kFocus);
  if (!r.ok) return false;
  // Structural prefix stays versioned even with content attached.
  if (r.blob.rfind("NAV1;v=1;", 0) != 0) return false;
  // The structural body still restores through the reused T1/FR-NAV-004
  // path when content is stripped at the first content boundary
  // (ASSUMPTION: content is appended after a `;content=` section; the
  // coder keeps the structural prefix parseable — reconcile, don't
  // re-derive, if the layout differs).
  std::string structural = r.blob;
  std::string::size_type pos = structural.find(";content=");
  if (pos != std::string::npos) structural.erase(pos);
  NavGraph h(kRoutes);
  NavResult rr = h.restore(structural);
  return rr.ok && h.serialize() == g.serialize();
}

bool check_opted_out_restores_empty_with_ok() {
  NavGraph g = makeGraph();
  SnapshotResult r = captureSnapshotWithContent(g, kOptOut, kFocusOut);
  if (!r.ok || r.error != SnapshotError::NONE) return false;
  // No content payload for any route: blob carries no scroll/focus data.
  if (r.blob.find("120") != std::string::npos) return false;
  if (r.blob.find("field=") != std::string::npos) return false;
  NavGraph h(kRoutes);
  NavResult rr = h.restore(r.blob);
  return rr.ok && h.serialize() == g.serialize();
}

bool check_partial_opt_in_only_opted_route_has_content() {
  NavGraph g(kRoutes);
  g.apply(NavOp{NavOpType::Push, "details", 0});
  g.apply(NavOp{NavOpType::SwitchTab, "", 1});
  g.apply(NavOp{NavOpType::Push, "settings", 1});
  ScrollProvider scroll = [](const std::string& routeId) -> std::string {
    if (routeId == "details") return "120";
    return "";
  };
  SnapshotResult r = captureSnapshotWithContent(g, scroll, kFocusOut);
  if (!r.ok) return false;
  return r.blob.find("120") != std::string::npos;
}

bool check_truncated_never_set() {
  NavGraph g = makeGraph();
  SnapshotResult r = captureSnapshotWithContent(g, kScroll, kFocus);
  // Error shape is T1's only: NONE on success, CORRUPT on failure.
  // There is no TRUNCATED outcome on this path.
  return r.ok ? (r.error == SnapshotError::NONE)
              : (r.error == SnapshotError::CORRUPT && r.blob.empty());
}

bool check_empty_providers_match_plain_capture_shape() {
  NavGraph g = makeGraph();
  SnapshotResult plain = captureSnapshot(g);
  SnapshotResult withContent =
      captureSnapshotWithContent(g, kOptOut, kFocusOut);
  if (!plain.ok || !withContent.ok) return false;
  return withContent.blob.rfind("NAV1;v=1;", 0) == 0 &&
         withContent.error == SnapshotError::NONE;
}

bool check_content_capture_never_throws() {
  NavGraph g = makeGraph();
  bool noThrow = true;
  SnapshotResult r{false, "", SnapshotError::CORRUPT};
  try {
    r = captureSnapshotWithContent(g, kScroll, kFocus);
  } catch (...) {
    noThrow = false;
  }
  return noThrow && r.ok;
}

}  // namespace

int main() {
  EXPECT_TRUE(check_opted_in_content_captured(), "opted_in_content_captured");
  EXPECT_TRUE(check_opted_in_content_round_trips_structurally(),
              "opted_in_content_round_trips_structurally");
  EXPECT_TRUE(check_opted_out_restores_empty_with_ok(),
              "opted_out_restores_empty_with_ok");
  EXPECT_TRUE(check_partial_opt_in_only_opted_route_has_content(),
              "partial_opt_in_only_opted_route_has_content");
  EXPECT_TRUE(check_truncated_never_set(), "truncated_never_set");
  EXPECT_TRUE(check_empty_providers_match_plain_capture_shape(),
              "empty_providers_match_plain_capture_shape");
  EXPECT_TRUE(check_content_capture_never_throws(),
              "content_capture_never_throws");
  if (failures > 0) {
    std::printf("snapshot_scroll_focus_test: %d failing\n", failures);
    return 1;
  }
  std::printf("snapshot_scroll_focus_test: all passing\n");
  return 0;
}

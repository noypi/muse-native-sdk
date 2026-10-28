// T1 contract tests — FR-NAV-005 / AC-NAV-008 (TC-NAV-008).
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services. Consults no implementation;
// binds to the T1 `Interface:` line.
//
// ASSUMPTION (NavOp shape): the plan `Interface:` line names `NavOp` but
// does not define its fields. These cases drive mutations through the
// `NavOp` struct declared in `navigation/graph.h` with the fields used
// below (`type`, `routeId`, `tabIndex`). Enum, struct and field names are
// the test's proposal; the T1 contract records the reconciliation rule
// (update call sites, not expectations) if the coder names them
// differently. Expected `NavResult`/`NavError` semantics follow the
// `Interface:` line verbatim.
// ASSUMPTION (construction): `NavGraph` is constructed with the registered
// route table, `explicit NavGraph(std::vector<std::string> routes)`, where
// element 0 is the home route. Unknown routeIds are those absent from it.
#include <cstdio>
#include <string>
#include <vector>

#include "navigation/graph.h"

namespace {

int failures = 0;

#define EXPECT_TRUE(cond, name)                                     \
  do {                                                              \
    if (!(cond)) {                                                  \
      std::printf("FAIL %s\n", name);                               \
      ++failures;                                                   \
    } else {                                                        \
      std::printf("PASS %s\n", name);                               \
    }                                                               \
  } while (0)

const std::vector<std::string> kRoutes = {"home", "details", "settings",
                                          "profile", "modal_help"};

bool check_push_pop_round_trip() {
  NavGraph g(kRoutes);
  NavResult r1 = g.apply(NavOp{NavOpType::Push, "details", 0});
  NavResult r2 = g.apply(NavOp{NavOpType::Push, "settings", 0});
  if (!r1.ok || !r2.ok) return false;
  std::string blob = g.serialize();
  NavGraph h(kRoutes);
  NavResult rr = h.restore(blob);
  return rr.ok && h.serialize() == blob;
}

bool check_pop_returns_toward_home() {
  NavGraph g(kRoutes);
  g.apply(NavOp{NavOpType::Push, "details", 0});
  NavResult r = g.apply(NavOp{NavOpType::Pop, "", 0});
  if (!r.ok) return false;
  std::string blob = g.serialize();
  NavGraph fresh(kRoutes);
  return fresh.serialize() == blob;
}

bool check_tab_switch_preserves_per_tab_stacks() {
  NavGraph g(kRoutes);
  g.apply(NavOp{NavOpType::Push, "details", 0});
  NavResult sw = g.apply(NavOp{NavOpType::SwitchTab, "", 1});
  if (!sw.ok) return false;
  g.apply(NavOp{NavOpType::Push, "profile", 0});
  std::string blob = g.serialize();
  NavGraph h(kRoutes);
  if (!h.restore(blob).ok) return false;
  return h.serialize() == blob;
}

bool check_modal_push_dismiss_round_trip() {
  NavGraph g(kRoutes);
  NavResult m = g.apply(NavOp{NavOpType::PushModal, "modal_help", 0});
  if (!m.ok) return false;
  std::string withModal = g.serialize();
  NavGraph h(kRoutes);
  if (!h.restore(withModal).ok || h.serialize() != withModal) return false;
  NavResult d = g.apply(NavOp{NavOpType::DismissModal, "", 0});
  if (!d.ok) return false;
  NavGraph fresh(kRoutes);
  return g.serialize() == fresh.serialize();
}

bool check_drawer_open_close_round_trip() {
  NavGraph g(kRoutes);
  if (!g.apply(NavOp{NavOpType::OpenDrawer, "", 0}).ok) return false;
  std::string open = g.serialize();
  NavGraph h(kRoutes);
  if (!h.restore(open).ok || h.serialize() != open) return false;
  if (!g.apply(NavOp{NavOpType::CloseDrawer, "", 0}).ok) return false;
  NavGraph fresh(kRoutes);
  return g.serialize() == fresh.serialize();
}

bool check_unknown_route_returns_error_without_mutation() {
  NavGraph g(kRoutes);
  g.apply(NavOp{NavOpType::Push, "details", 0});
  std::string before = g.serialize();
  NavResult r = g.apply(NavOp{NavOpType::Push, "no_such_route", 0});
  return !r.ok && r.error == NavError::UNKNOWN_ROUTE &&
         g.serialize() == before;
}

bool check_restore_corrupt_blob_reports_schema_mismatch() {
  NavGraph g(kRoutes);
  NavResult r = g.restore("not-a-nav-blob\x00\x01");
  return !r.ok && r.error == NavError::SCHEMA_MISMATCH;
}

bool check_serialize_is_deterministic() {
  NavGraph g(kRoutes);
  g.apply(NavOp{NavOpType::Push, "details", 0});
  g.apply(NavOp{NavOpType::PushModal, "modal_help", 0});
  return g.serialize() == g.serialize();
}

}  // namespace

int main() {
  EXPECT_TRUE(check_push_pop_round_trip(), "push_pop_round_trip");
  EXPECT_TRUE(check_pop_returns_toward_home(), "pop_returns_toward_home");
  EXPECT_TRUE(check_tab_switch_preserves_per_tab_stacks(),
              "tab_switch_preserves_per_tab_stacks");
  EXPECT_TRUE(check_modal_push_dismiss_round_trip(),
              "modal_push_dismiss_round_trip");
  EXPECT_TRUE(check_drawer_open_close_round_trip(),
              "drawer_open_close_round_trip");
  EXPECT_TRUE(check_unknown_route_returns_error_without_mutation(),
              "unknown_route_no_mutation");
  EXPECT_TRUE(check_restore_corrupt_blob_reports_schema_mismatch(),
              "restore_corrupt_schema_mismatch");
  EXPECT_TRUE(check_serialize_is_deterministic(), "serialize_deterministic");
  if (failures > 0) {
    std::printf("graph_unification_test: %d failing\n", failures);
    return 1;
  }
  std::printf("graph_unification_test: all passing\n");
  return 0;
}

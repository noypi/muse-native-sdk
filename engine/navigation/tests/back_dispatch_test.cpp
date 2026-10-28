// T10 contract tests — FR-NAV-010 / AC-NAV-030 (TC-NAV-030).
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services.
//
// Binds to the plan T10 `Interface:` line (verbatim):
//   `void registerGraph(const std::string& graphId, int priority);`
//   `void unregisterGraph(const std::string& graphId);`
//   `std::string dispatchBack();`
// (returns handling graphId, or `"home"` when none claims it)
//
// ASSUMPTION (free functions / global state): the plan names unqualified
// free functions; tests call `registerGraph`/`unregisterGraph`/`dispatchBack`
// in the global namespace (mirroring the T9 `routeEntry` precedent). Dispatch
// state is therefore process-global; each check uses distinct graphIds and
// unregisters everything it registered so checks stay independent.
// ASSUMPTION (precedence): higher `priority` wins; modal > stack > tab is
// expressed as modal=30, stack=20, tab=10. Dispatch returns the registered
// graph with the highest priority; with no graphs registered it returns
// `"home"` (back-to-home). Re-registering an id replaces its priority.
// ASSUMPTION (teardown): `unregisterGraph` removes the callback so a
// destroyed graph never intercepts a later `dispatchBack()`; unregistering
// an unknown id is a no-op (no crash).
#include <cstdio>
#include <string>

#include "navigation/back_dispatch.h"

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

bool check_dispatch_empty_returns_home() {
  return dispatchBack() == "home";
}

bool check_modal_first_with_all_registered() {
  registerGraph("t10-modal", 30);
  registerGraph("t10-stack", 20);
  registerGraph("t10-tab", 10);
  std::string got = dispatchBack();
  unregisterGraph("t10-modal");
  unregisterGraph("t10-stack");
  unregisterGraph("t10-tab");
  return got == "t10-modal";
}

bool check_stack_after_modal_teardown() {
  registerGraph("t10-stack2", 20);
  registerGraph("t10-tab2", 10);
  registerGraph("t10-modal2", 30);
  unregisterGraph("t10-modal2");  // modal teardown
  std::string got = dispatchBack();
  unregisterGraph("t10-stack2");
  unregisterGraph("t10-tab2");
  return got == "t10-stack2";
}

bool check_tab_after_stack_teardown_then_home() {
  registerGraph("t10-stack3", 20);
  registerGraph("t10-tab3", 10);
  unregisterGraph("t10-stack3");  // stack teardown
  std::string onlyTab = dispatchBack();
  unregisterGraph("t10-tab3");  // tab teardown
  std::string none = dispatchBack();
  return onlyTab == "t10-tab3" && none == "home";
}

bool check_unregistered_graph_never_intercepts() {
  registerGraph("t10-live", 20);
  registerGraph("t10-dead", 99);  // highest priority, then torn down
  unregisterGraph("t10-dead");
  std::string got = dispatchBack();
  unregisterGraph("t10-live");
  return got == "t10-live";
}

bool check_unregister_unknown_id_is_noop() {
  unregisterGraph("t10-never-existed");
  return dispatchBack() == "home";
}

bool check_reregister_replaces_priority() {
  registerGraph("t10-flex", 10);
  registerGraph("t10-rival", 20);
  std::string before = dispatchBack();
  registerGraph("t10-flex", 50);  // promote above rival
  std::string after = dispatchBack();
  unregisterGraph("t10-flex");
  unregisterGraph("t10-rival");
  return before == "t10-rival" && after == "t10-flex";
}

}  // namespace

int main() {
  EXPECT_TRUE(check_dispatch_empty_returns_home(),
              "dispatch_empty_returns_home");
  EXPECT_TRUE(check_modal_first_with_all_registered(),
              "modal_first_with_all_registered");
  EXPECT_TRUE(check_stack_after_modal_teardown(),
              "stack_after_modal_teardown");
  EXPECT_TRUE(check_tab_after_stack_teardown_then_home(),
              "tab_after_stack_teardown_then_home");
  EXPECT_TRUE(check_unregistered_graph_never_intercepts(),
              "unregistered_graph_never_intercepts");
  EXPECT_TRUE(check_unregister_unknown_id_is_noop(),
              "unregister_unknown_id_is_noop");
  EXPECT_TRUE(check_reregister_replaces_priority(),
              "reregister_replaces_priority");
  if (failures == 0) {
    std::printf("ALL PASS\n");
  }
  return failures == 0 ? 0 : 1;
}

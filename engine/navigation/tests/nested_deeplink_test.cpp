// T3 contract tests — FR-NAV-007 / AC-NAV-018 (TC-NAV-018).
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services.
// Binds to the plan T3 `Interface:` line:
//   `NestedResult resolveNested(const NestedManifest& m,
//    const std::string& host, const std::vector<std::string>& path);`
// where `NestedManifest{graphs:vector<GraphDecl>}`,
// `GraphDecl{graphId:string, parent:string, routes:vector<string>}` and
// `NestedResult{ok:bool, stack:vector<string>, error:NavError}`.
// `NavError` is reused from `navigation/graph.h`; this file declares no
// competing enum.
//
// ASSUMPTION (unknown-nested shape): the plan requires only that an
// unknown nested route "degrades to home" (FR-NAV-004 observed). Tests
// bind `stack == {"home"}`; whether `ok` is true/false and which
// `NavError` value accompanies the fallback is NOT asserted here.
// ASSUMPTION (host/path split): tests pass host `"app"` with path
// `{"tab-2", "item3"}`; the leaf's parent chain is resolved from the
// manifest `parent` links, not from extra path segments.
#include <cstdio>
#include <string>
#include <vector>

#include "navigation/graph.h"
#include "navigation/nested_graph.h"

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

bool check_deeplink_builds_full_parent_chain() {
  NestedManifest m = makeTabManifest();
  NestedResult r = resolveNested(m, "app", {"tab-2", "item3"});
  const std::vector<std::string> want = {"home", "item1", "item2", "item3"};
  return r.ok && r.error == NavError::NONE && r.stack == want;
}

bool check_pops_walk_back_through_chain() {
  NestedManifest m = makeTabManifest();
  NestedResult r = resolveNested(m, "app", {"tab-2", "item3"});
  if (!r.ok || r.stack.size() != 4) return false;
  // Simulate sequential back pops on the returned stack: each pop must
  // reveal the previous chain element (item2 -> item1 -> home).
  std::vector<std::string> stack = r.stack;
  stack.pop_back();
  if (stack.back() != "item2") return false;
  stack.pop_back();
  if (stack.back() != "item1") return false;
  stack.pop_back();
  return stack.back() == "home" && stack.size() == 1;
}

bool check_leaf_item1_yields_short_chain() {
  NestedManifest m = makeTabManifest();
  NestedResult r = resolveNested(m, "app", {"tab-2", "item1"});
  const std::vector<std::string> want = {"home", "item1"};
  return r.ok && r.stack == want;
}

bool check_unknown_nested_degrades_to_home() {
  NestedManifest m = makeTabManifest();
  NestedResult r = resolveNested(m, "app", {"tab-2", "nope"});
  // Binding assertion is the degraded stack only (see ASSUMPTION above).
  const std::vector<std::string> want = {"home"};
  return r.stack == want;
}

bool check_unknown_graph_degrades_to_home() {
  NestedManifest m = makeTabManifest();
  NestedResult r = resolveNested(m, "app", {"tab-9", "item1"});
  const std::vector<std::string> want = {"home"};
  return r.stack == want;
}

bool check_wrong_host_degrades_to_home() {
  NestedManifest m = makeTabManifest();
  NestedResult r = resolveNested(m, "evil", {"tab-2", "item3"});
  // Host allowlist: foreign host must degrade to home stack.
  const std::vector<std::string> want = {"home"};
  return r.stack == want;
}

}  // namespace

int main() {
  EXPECT_TRUE(check_deeplink_builds_full_parent_chain(),
              "deeplink_builds_full_parent_chain");
  EXPECT_TRUE(check_pops_walk_back_through_chain(),
              "pops_walk_back_through_chain");
  EXPECT_TRUE(check_leaf_item1_yields_short_chain(),
              "leaf_item1_yields_short_chain");
  EXPECT_TRUE(check_unknown_nested_degrades_to_home(),
              "unknown_nested_degrades_to_home");
  EXPECT_TRUE(check_unknown_graph_degrades_to_home(),
              "unknown_graph_degrades_to_home");
  EXPECT_TRUE(check_wrong_host_degrades_to_home(),
              "wrong_host_degrades_to_home");
  if (failures > 0) {
    std::printf("nested_deeplink_test: %d failing\n", failures);
    return 1;
  }
  std::printf("nested_deeplink_test: all passing\n");
  return 0;
}

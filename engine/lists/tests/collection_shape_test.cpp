// T10 contract tests — FR-LISTS-005 / AC-LISTS-027 (TC-LISTS-027).
// Hermetic: C++17 + lists headers only; no network, no env/config reads,
// no filesystem, no live services.
// Binds to the T10 `Interface:` lines:
//   `bool ReflowSpan(CollectionLayout& l, ItemKey k, int newSpan);`
//   returns true with only affected cells re-laid-out.
//   `bool VerifyShapeScenario(const ScenarioResult& r);`
//   checks contacts-jump + reflow + state-switch outcomes.
// Detail under test: integration over T6–T9 — contacts list with index bar
// jumps instantly; span change reflows without whole-list rebind; state
// switching preserves position.
//
// ASSUMPTION (type spellings): the plan `Interface:` line does not settle
// the C++ namespace or `ScenarioResult` shape. This test proposes:
// namespace `lists`; `CollectionLayout` from `lists/collection_contract.h`;
// `struct ScenarioResult{bool jumpOk; bool reflowOk; bool stateSwitchOk;}`.
// Reconciliation rule: if the coder names fields differently, update these
// call sites — not the expected behavior (reflow locality + scenario check).
//
// ASSUMPTION (reflow locality): a successful reflow changes the target
// cell's span (and its frame) while leaving all other cells' keys, spans,
// and frames untouched — the observable proxy for "only affected cells
// re-laid-out" through this signature.
//
// ASSUMPTION (invalid span): `newSpan < 1` (or beyond the layout's span
// count) returns false and mutates nothing.
#include <cstdio>
#include <string>
#include <vector>

#include "lists/collection_contract.h"
#include "lists/collection_shape.h"
#include "lists/stable_key.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
  if (ok) {
    std::printf("PASS %s\n", name);
  } else {
    std::printf("FAIL %s\n", name);
    ++g_failures;
  }
}

lists::CollectionLayout grid4() {
  lists::ItemList items;
  for (int i = 0; i < 4; ++i) items.push_back(lists::ItemKey{"c" + std::to_string(i)});
  return lists::BuildLayout({lists::CollectionMode::GridSpans, 2}, items);
}

void case_reflow_changes_target_only() {
  lists::CollectionLayout l = grid4();
  auto before = l.frames;
  bool ok = lists::ReflowSpan(l, lists::ItemKey{"c1"}, 2);
  check(ok, "reflow_returns_true");
  check(l.frames.size() == before.size(), "reflow_keeps_count");
  bool others_same = true;
  for (size_t i = 0; i < l.frames.size(); ++i) {
    if (l.frames[i].key == lists::ItemKey{"c1"}) continue;
    if (l.frames[i].span != before[i].span) others_same = false;
    if (l.frames[i].x != before[i].x || l.frames[i].y != before[i].y ||
        l.frames[i].w != before[i].w || l.frames[i].h != before[i].h) {
      others_same = false;
    }
  }
  check(others_same, "reflow_only_affected_cells");
  bool target_changed = false;
  for (const auto& f : l.frames) {
    if (f.key == lists::ItemKey{"c1"} && f.span == 2) target_changed = true;
  }
  check(target_changed, "reflow_target_span_updated");
}

void case_reflow_unknown_key_false() {
  lists::CollectionLayout l = grid4();
  auto before = l.frames;
  bool ok = lists::ReflowSpan(l, lists::ItemKey{"nope"}, 2);
  check(!ok, "reflow_unknown_returns_false");
  bool same = l.frames.size() == before.size();
  for (size_t i = 0; same && i < l.frames.size(); ++i) {
    same = l.frames[i].span == before[i].span;
  }
  check(same, "reflow_unknown_no_mutation");
}

void case_reflow_invalid_span_false() {
  lists::CollectionLayout l = grid4();
  auto before = l.frames;
  bool ok = lists::ReflowSpan(l, lists::ItemKey{"c0"}, 0);
  check(!ok, "reflow_zero_span_false");
  bool same = l.frames.size() == before.size();
  for (size_t i = 0; same && i < l.frames.size(); ++i) {
    same = l.frames[i].span == before[i].span;
  }
  check(same, "reflow_invalid_no_mutation");
}

void case_verify_all_ok() {
  lists::ScenarioResult r{true, true, true};
  check(lists::VerifyShapeScenario(r), "verify_all_ok_true");
}

void case_verify_any_fail() {
  check(!lists::VerifyShapeScenario({false, true, true}), "verify_jump_fail_false");
  check(!lists::VerifyShapeScenario({true, false, true}), "verify_reflow_fail_false");
  check(!lists::VerifyShapeScenario({true, true, false}), "verify_switch_fail_false");
  check(!lists::VerifyShapeScenario({false, false, false}), "verify_all_fail_false");
}

}  // namespace

int main() {
  case_reflow_changes_target_only();
  case_reflow_unknown_key_false();
  case_reflow_invalid_span_false();
  case_verify_all_ok();
  case_verify_any_fail();
  if (g_failures == 0) {
    std::printf("ALL TESTS PASSED\n");
  } else {
    std::printf("%d TEST(S) FAILED\n", g_failures);
  }
  return g_failures == 0 ? 0 : 1;
}

// T6 contract tests — FR-LISTS-005 / AC-LISTS-023 (TC-LISTS-023).
// Hermetic: C++17 + lists headers only; no network, no env/config reads,
// no filesystem, no live services.
// Binds to the T6 `Interface:` line:
//   `CollectionLayout BuildLayout(CollectionSpec spec, const ItemList& items);`
//   where `CollectionSpec{mode: List | GridSpans | Staggered, spanCount}`
// Detail under test: modes are layout parameters over one recycler/diff
// path (no per-mode forks); heterogeneous holder types pooled per shape key.
// Include path follows the engine convention:
//   #include "lists/collection_contract.h" with engine/lists/include on path.
//
// ASSUMPTION (type spellings): the plan `Interface:` line does not settle
// the C++ namespace, enum spelling, or struct shapes. This test proposes:
// namespace `lists`; `enum class CollectionMode{List, GridSpans, Staggered}`;
// `CollectionSpec{CollectionMode mode; int spanCount;}`;
// `using ItemList = std::vector<ItemKey>` (ItemKey from lists/stable_key.h);
// `struct ItemFrame{ItemKey key; float x, y, w, h; int span;}`;
// `struct CollectionLayout{std::vector<ItemFrame> frames; std::vector<std::string> poolKeys;}`.
// Reconciliation rule: if the coder names them differently, update these
// call sites — not the expected behavior (correct frames per mode, shared pool).
//
// ASSUMPTION (shared pool): `poolKeys` is identical across modes for the
// same item set (one recycler/diff path); exact pool key strings are the
// coder's choice — only cross-mode equality is asserted.
//
// ASSUMPTION (geometry): List mode stacks full-width frames in order
// (x==0, y non-decreasing); GridSpans packs spans within spanCount columns;
// Staggered keeps item order with varying heights. Exact pixels are the
// coder's choice — only structural invariants are asserted.
#include <cstdio>
#include <string>
#include <vector>

#include "lists/collection_contract.h"
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

lists::ItemList items5() {
  lists::ItemList v;
  for (int i = 0; i < 5; ++i) v.push_back(lists::ItemKey{"k" + std::to_string(i)});
  return v;
}

bool ordered_keys(const lists::CollectionLayout& l, const lists::ItemList& want) {
  if (l.frames.size() != want.size()) return false;
  for (size_t i = 0; i < want.size(); ++i) {
    if (l.frames[i].key != want[i]) return false;
  }
  return true;
}

void case_list_stacks_full_width() {
  lists::CollectionSpec spec{lists::CollectionMode::List, 1};
  lists::CollectionLayout l = lists::BuildLayout(spec, items5());
  check(l.frames.size() == 5, "list_frame_count");
  check(ordered_keys(l, items5()), "list_preserves_order");
  bool stacked = true;
  for (const auto& f : l.frames) {
    if (f.x != 0.0f) stacked = false;
  }
  for (size_t i = 1; i < l.frames.size(); ++i) {
    if (l.frames[i].y < l.frames[i - 1].y) stacked = false;
  }
  check(stacked, "list_stacks_vertically");
}

void case_grid_respects_span_count() {
  lists::CollectionSpec spec{lists::CollectionMode::GridSpans, 2};
  lists::CollectionLayout l = lists::BuildLayout(spec, items5());
  check(l.frames.size() == 5, "grid_frame_count");
  check(ordered_keys(l, items5()), "grid_preserves_order");
  bool bounded = true;
  for (const auto& f : l.frames) {
    if (f.span < 1 || f.span > spec.spanCount) bounded = false;
    if (f.x < 0.0f || f.x + f.w <= 0.0f) bounded = false;
  }
  check(bounded, "grid_spans_bounded");
}

void case_staggered_keeps_order() {
  lists::CollectionSpec spec{lists::CollectionMode::Staggered, 2};
  lists::CollectionLayout l = lists::BuildLayout(spec, items5());
  check(l.frames.size() == 5, "staggered_frame_count");
  check(ordered_keys(l, items5()), "staggered_preserves_order");
}

void case_shared_pool_across_modes() {
  auto items = items5();
  lists::CollectionLayout a =
      lists::BuildLayout({lists::CollectionMode::List, 1}, items);
  lists::CollectionLayout b =
      lists::BuildLayout({lists::CollectionMode::GridSpans, 2}, items);
  lists::CollectionLayout c =
      lists::BuildLayout({lists::CollectionMode::Staggered, 2}, items);
  check(!a.poolKeys.empty(), "pool_nonempty");
  check(a.poolKeys == b.poolKeys && b.poolKeys == c.poolKeys, "pool_shared_across_modes");
}

void case_empty_items_empty_layout() {
  lists::ItemList empty;
  lists::CollectionLayout l =
      lists::BuildLayout({lists::CollectionMode::List, 1}, empty);
  check(l.frames.empty(), "empty_items_empty_frames");
}

void case_zero_span_count_safe() {
  lists::CollectionSpec spec{lists::CollectionMode::GridSpans, 0};
  lists::CollectionLayout l = lists::BuildLayout(spec, items5());
  check(l.frames.size() == 5, "zero_spancount_still_lays_out");
}

}  // namespace

int main() {
  case_list_stacks_full_width();
  case_grid_respects_span_count();
  case_staggered_keeps_order();
  case_shared_pool_across_modes();
  case_empty_items_empty_layout();
  case_zero_span_count_safe();
  if (g_failures == 0) {
    std::printf("ALL TESTS PASSED\n");
  } else {
    std::printf("%d TEST(S) FAILED\n", g_failures);
  }
  return g_failures == 0 ? 0 : 1;
}

// T12 contract tests — FR-LISTS-006 / AC-LISTS-029 (TC-LISTS-029).
// Hermetic: C++17 + lists headers only; no network, no env/config reads,
// no filesystem, no live services.
// Binds to the T12 `Interface:` line:
//   `CollectionItemInfo GetItemInfo(const CollectionNode& n, ItemKey k);`
//   returns `{index, count, row, column, rowSpan, colSpan}` — stable
//   logical position including span cells.
// Detail under test: index/count from logical order, row/column/span from
// the T6 layout; span items report correct multi-cell extents.
//
// ASSUMPTION (type spellings): the plan `Interface:` line does not settle
// the C++ namespace or struct shapes. This test proposes:
// namespace `lists`; `CollectionNode` from `lists/collection_semantics.h`
// with an ordered `items` member (`std::vector<ItemKey>`) plus the T11
// window fields; `struct CollectionItemInfo{int index; int count; int row;
// int column; int rowSpan; int colSpan;}`.
// Reconciliation rule: if the coder names them differently, update these
// call sites — not the expected behavior (exact logical position + spans).
//
// ASSUMPTION (grid geometry): tests build the node in GridSpans mode with
// spanCount 2 via `BuildLayout`; row/column derive from the T6 layout
// frames in order; list-mode items report row == index, column == 0,
// rowSpan == colSpan == 1.
//
// ASSUMPTION (unknown key): querying a key absent from the node reports
// index == -1 (count still equals the node size; spans are 1).
#include <cstdio>
#include <string>
#include <vector>

#include "lists/collection_item_info.h"
#include "lists/collection_semantics.h"
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

lists::CollectionNode list5() {
  lists::CollectionNode n;
  n.totalCount = 5;
  n.realizedCount = 5;
  n.leadingIndex = 0;
  for (int i = 0; i < 5; ++i) n.items.push_back(lists::ItemKey{"k" + std::to_string(i)});
  return n;
}

void case_list_index_and_count() {
  auto n = list5();
  lists::CollectionItemInfo info = lists::GetItemInfo(n, lists::ItemKey{"k2"});
  check(info.index == 2, "list_index");
  check(info.count == 5, "list_count");
}

void case_list_row_column_span() {
  auto n = list5();
  lists::CollectionItemInfo info = lists::GetItemInfo(n, lists::ItemKey{"k3"});
  check(info.row == 3, "list_row_eq_index");
  check(info.column == 0, "list_column_zero");
  check(info.rowSpan == 1, "list_rowspan_one");
  check(info.colSpan == 1, "list_colspan_one");
}

void case_first_and_last_index() {
  auto n = list5();
  check(lists::GetItemInfo(n, lists::ItemKey{"k0"}).index == 0, "first_index_zero");
  check(lists::GetItemInfo(n, lists::ItemKey{"k4"}).index == 4, "last_index_four");
  check(lists::GetItemInfo(n, lists::ItemKey{"k4"}).count == 5, "last_count_five");
}

void case_count_stable_across_queries() {
  auto n = list5();
  bool stable = true;
  for (int i = 0; i < 5; ++i) {
    auto info = lists::GetItemInfo(n, lists::ItemKey{"k" + std::to_string(i)});
    if (info.count != 5 || info.index != i) stable = false;
  }
  check(stable, "count_stable");
}

void case_unknown_key_negative_index() {
  auto n = list5();
  lists::CollectionItemInfo info = lists::GetItemInfo(n, lists::ItemKey{"nope"});
  check(info.index == -1, "unknown_index_neg");
  check(info.count == 5, "unknown_count_still_size");
}

void case_grid_row_column() {
  // GridSpans spanCount 2 over 4 items: rows 0,0,1,1 / columns 0,1,0,1.
  lists::CollectionNode n;
  n.totalCount = 4;
  n.realizedCount = 4;
  n.leadingIndex = 0;
  n.mode = lists::CollectionMode::GridSpans;
  n.spanCount = 2;
  for (int i = 0; i < 4; ++i) n.items.push_back(lists::ItemKey{"g" + std::to_string(i)});
  lists::CollectionItemInfo i0 = lists::GetItemInfo(n, lists::ItemKey{"g0"});
  lists::CollectionItemInfo i1 = lists::GetItemInfo(n, lists::ItemKey{"g1"});
  lists::CollectionItemInfo i2 = lists::GetItemInfo(n, lists::ItemKey{"g2"});
  check(i0.row == 0 && i0.column == 0, "grid_r0c0");
  check(i1.row == 0 && i1.column == 1, "grid_r0c1");
  check(i2.row == 1 && i2.column == 0, "grid_r1c0");
  check(i2.index == 2 && i2.count == 4, "grid_index_count");
}

void case_span_item_extents() {
  // A span-2 item in a spanCount-2 grid covers both columns of its row.
  lists::CollectionNode n;
  n.totalCount = 3;
  n.realizedCount = 3;
  n.leadingIndex = 0;
  n.mode = lists::CollectionMode::GridSpans;
  n.spanCount = 2;
  n.items.push_back(lists::ItemKey{"s0"});
  n.items.push_back(lists::ItemKey{"wide"});
  n.items.push_back(lists::ItemKey{"s2"});
  n.spans.push_back(1);
  n.spans.push_back(2);
  n.spans.push_back(1);
  lists::CollectionItemInfo w = lists::GetItemInfo(n, lists::ItemKey{"wide"});
  check(w.colSpan == 2, "span_colspan_two");
  check(w.rowSpan == 1, "span_rowspan_one");
  check(w.index == 1 && w.count == 3, "span_index_count");
}

}  // namespace

int main() {
  case_list_index_and_count();
  case_list_row_column_span();
  case_first_and_last_index();
  case_count_stable_across_queries();
  case_unknown_key_negative_index();
  case_grid_row_column();
  case_span_item_extents();
  if (g_failures == 0) {
    std::printf("ALL TESTS PASSED\n");
  } else {
    std::printf("%d TEST(S) FAILED\n", g_failures);
  }
  return g_failures == 0 ? 0 : 1;
}

// T4 contract tests — FR-LISTS-004 / AC-LISTS-021 (TC-LISTS-021).
// Hermetic: C++17 + lists headers only; no network, no env/config reads,
// no filesystem, no live services.
// Binds to the T4 `Interface:` lines:
//   `void ApplyRangeOp(SelectionSet& sel, RangeOp op, ItemKey anchor, ItemKey focus);`
//   with `RangeOp ∈ {Clear, Toggle, ExtendShift, AddCtrl, SelectAll}`.
//   `ActionBarState DeriveActionBar(const SelectionSet& sel);`
//   returns {visible, count, actions}.
// Detail under test: stable-key identity only (no positional selection);
// shift extends from anchor, ctrl toggles, select-all fills; action-bar
// visibility derives from non-empty selection.
// Include path follows the engine convention:
//   #include "lists/selection_mode.h" with engine/lists/include on the path.
//
// ASSUMPTION (type spellings): the plan `Interface:` line does not settle
// the C++ namespace, the `SelectionSet` shape, or the `ActionBarState`
// shape. This test proposes: namespace `lists`; `RangeOp::{Clear, Toggle,
// ExtendShift, AddCtrl, SelectAll}`; `SelectionSet{std::vector<
// std::string> universe; std::set<std::string> selected; }` where
// `universe` is the full key order (needed by ExtendShift/SelectAll) and
// `selected` the key-identity set; `ActionBarState{bool visible; size_t
// count; std::vector<std::string> actions; }`; `ItemKey` from
// `lists/stable_key.h`.
// Reconciliation rule: if the coder names them differently, update these
// call sites — not the expected behavior (exact key sets, derived bar).
//
// ASSUMPTION (ExtendShift range): shift selects the inclusive universe
// interval between anchor and focus (either direction); anchor==focus
// selects the single key. Unknown anchor/focus keys select just the focus
// key when it is known, else change nothing.
//
// ASSUMPTION (Toggle/AddCtrl): `Toggle` flips the focus key; `AddCtrl`
// adds the focus key without clearing (ctrl-toggle-off is expressed via
// `Toggle`). `SelectAll` fills from `universe`.
//
// ASSUMPTION (actions): a visible bar exposes a non-empty action list;
// exact action ids are the coder's choice (LAYOUT-M3X rendering is out of
// scope) — only non-emptiness is asserted.
#include <cstdio>
#include <string>
#include <vector>

#include "lists/selection_mode.h"
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

lists::SelectionSet sel10() {
  lists::SelectionSet s{};
  for (size_t i = 0; i < 10; ++i) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "k%02zu", i);
    s.universe.emplace_back(buf);
  }
  return s;
}

bool selected_is(const lists::SelectionSet& s,
                 std::vector<std::string> want) {
  std::vector<std::string> got(s.selected.begin(), s.selected.end());
  return got == want;
}

// Toggle adds an absent key and removes a present one.
void case_toggle_flips() {
  lists::SelectionSet s = sel10();
  lists::ApplyRangeOp(s, lists::RangeOp::Toggle, lists::ItemKey{"k03"},
                      lists::ItemKey{"k03"});
  check(selected_is(s, {"k03"}), "toggle_adds");
  lists::ApplyRangeOp(s, lists::RangeOp::Toggle, lists::ItemKey{"k03"},
                      lists::ItemKey{"k03"});
  check(s.selected.empty(), "toggle_removes");
}

// Clear empties (and is a no-op on empty).
void case_clear_empties() {
  lists::SelectionSet s = sel10();
  lists::ApplyRangeOp(s, lists::RangeOp::Toggle, lists::ItemKey{"k01"},
                      lists::ItemKey{"k01"});
  lists::ApplyRangeOp(s, lists::RangeOp::Toggle, lists::ItemKey{"k02"},
                      lists::ItemKey{"k02"});
  lists::ApplyRangeOp(s, lists::RangeOp::Clear, lists::ItemKey{""},
                      lists::ItemKey{""});
  check(s.selected.empty(), "clear_empties");
  lists::ApplyRangeOp(s, lists::RangeOp::Clear, lists::ItemKey{""},
                      lists::ItemKey{""});
  check(s.selected.empty(), "clear_empty_noop");
}

// TC-LISTS-021: shift extends the inclusive anchor..focus interval.
void case_shift_extends_forward() {
  lists::SelectionSet s = sel10();
  lists::ApplyRangeOp(s, lists::RangeOp::ExtendShift, lists::ItemKey{"k02"},
                      lists::ItemKey{"k05"});
  check(selected_is(s, {"k02", "k03", "k04", "k05"}),
        "shift_extends_forward");
}

// Reverse direction yields the same set.
void case_shift_extends_reverse() {
  lists::SelectionSet s = sel10();
  lists::ApplyRangeOp(s, lists::RangeOp::ExtendShift, lists::ItemKey{"k05"},
                      lists::ItemKey{"k02"});
  check(selected_is(s, {"k02", "k03", "k04", "k05"}),
        "shift_extends_reverse");
}

// Anchor == focus selects the single key.
void case_shift_single_point() {
  lists::SelectionSet s = sel10();
  lists::ApplyRangeOp(s, lists::RangeOp::ExtendShift, lists::ItemKey{"k04"},
                      lists::ItemKey{"k04"});
  check(selected_is(s, {"k04"}), "shift_single_point");
}

// TC-LISTS-021: ctrl adds without clearing the existing selection.
void case_ctrl_adds_without_clearing() {
  lists::SelectionSet s = sel10();
  lists::ApplyRangeOp(s, lists::RangeOp::ExtendShift, lists::ItemKey{"k02"},
                      lists::ItemKey{"k03"});
  lists::ApplyRangeOp(s, lists::RangeOp::AddCtrl, lists::ItemKey{"k02"},
                      lists::ItemKey{"k07"});
  check(selected_is(s, {"k02", "k03", "k07"}),
        "ctrl_adds_without_clearing");
}

// TC-LISTS-021: select-all fills the exact universe key set.
void case_select_all_fills() {
  lists::SelectionSet s = sel10();
  lists::ApplyRangeOp(s, lists::RangeOp::SelectAll, lists::ItemKey{""},
                      lists::ItemKey{""});
  check(selected_is(s, s.universe), "select_all_fills");
}

// Selection is key identity: reordering the universe leaves it intact.
void case_key_stable_across_reorder() {
  lists::SelectionSet s = sel10();
  lists::ApplyRangeOp(s, lists::RangeOp::Toggle, lists::ItemKey{"k03"},
                      lists::ItemKey{"k03"});
  lists::ApplyRangeOp(s, lists::RangeOp::Toggle, lists::ItemKey{"k07"},
                      lists::ItemKey{"k07"});
  // Reverse the backing order (positional selection would break).
  lists::SelectionSet r = s;
  r.universe.assign(s.universe.rbegin(), s.universe.rend());
  r.selected = s.selected;
  check(selected_is(r, {"k03", "k07"}), "key_stable_across_reorder");
}

// Empty selection hides the bar with count zero.
void case_bar_hidden_when_empty() {
  lists::SelectionSet s = sel10();
  lists::ActionBarState b = lists::DeriveActionBar(s);
  check(!b.visible && b.count == 0, "bar_hidden_when_empty");
}

// Non-empty selection shows the bar with the exact count and actions.
void case_bar_shows_count() {
  lists::SelectionSet s = sel10();
  lists::ApplyRangeOp(s, lists::RangeOp::ExtendShift, lists::ItemKey{"k01"},
                      lists::ItemKey{"k03"});
  lists::ActionBarState b = lists::DeriveActionBar(s);
  check(b.visible && b.count == 3, "bar_shows_count");
  check(!b.actions.empty(), "bar_has_actions");
}

// Clearing the selection hides the bar again.
void case_bar_hides_after_clear() {
  lists::SelectionSet s = sel10();
  lists::ApplyRangeOp(s, lists::RangeOp::SelectAll, lists::ItemKey{""},
                      lists::ItemKey{""});
  check(lists::DeriveActionBar(s).visible, "bar_visible_when_full");
  lists::ApplyRangeOp(s, lists::RangeOp::Clear, lists::ItemKey{""},
                      lists::ItemKey{""});
  lists::ActionBarState b = lists::DeriveActionBar(s);
  check(!b.visible && b.count == 0, "bar_hides_after_clear");
}

}  // namespace

int main() {
  case_toggle_flips();
  case_clear_empties();
  case_shift_extends_forward();
  case_shift_extends_reverse();
  case_shift_single_point();
  case_ctrl_adds_without_clearing();
  case_select_all_fills();
  case_key_stable_across_reorder();
  case_bar_hidden_when_empty();
  case_bar_shows_count();
  case_bar_hides_after_clear();
  if (g_failures == 0) {
    std::printf("ALL TESTS PASSED\n");
  } else {
    std::printf("%d TEST(S) FAILED\n", g_failures);
  }
  return g_failures == 0 ? 0 : 1;
}

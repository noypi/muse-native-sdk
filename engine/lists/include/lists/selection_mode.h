#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "lists/stable_key.h"

namespace lists {

// Selection range op for keyboard-driven multi-select (TC-LISTS-021).
enum class RangeOp { Clear, Toggle, ExtendShift, AddCtrl, SelectAll };

// Key-identity selection state. `universe` is the full key order (needed
// by ExtendShift/SelectAll); `selected` is the key-identity set — matching
// is by key, never by position, so reordering `universe` leaves it intact.
struct SelectionSet {
  std::vector<std::string> universe;
  std::set<std::string> selected;
};

// Derived action-bar model (LAYOUT-M3X rendering itself is out of scope).
// `visible` derives from non-empty selection; `count` is the exact
// selection size; `actions` is non-empty whenever visible.
struct ActionBarState {
  bool visible = false;
  size_t count = 0;
  std::vector<std::string> actions;
};

// Applies one keyboard range op to `sel`:
// - Clear: empties the selection.
// - Toggle: flips the focus key (present in `universe` only).
// - ExtendShift: replaces the selection with the inclusive `universe`
//   interval anchor..focus (either direction); anchor==focus selects the
//   single key. Unknown anchor selects just the focus key when it is known;
//   unknown focus changes nothing.
// - AddCtrl: adds the focus key without clearing (known keys only).
// - SelectAll: fills the selection from `universe`.
void ApplyRangeOp(SelectionSet& sel, RangeOp op, ItemKey anchor, ItemKey focus);

// Derives the action-bar model from the selection: hidden with count 0
// when empty, visible with the exact count and a non-empty action list
// otherwise.
ActionBarState DeriveActionBar(const SelectionSet& sel);

}  // namespace lists

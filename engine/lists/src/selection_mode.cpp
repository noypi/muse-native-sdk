#include "lists/selection_mode.h"

#include <algorithm>

namespace lists {
namespace {

bool InUniverse(const SelectionSet& sel, const std::string& key) {
  if (key.empty()) {
    return false;
  }
  return std::find(sel.universe.begin(), sel.universe.end(), key) !=
         sel.universe.end();
}

}  // namespace

void ApplyRangeOp(SelectionSet& sel, RangeOp op, ItemKey anchor,
                  ItemKey focus) {
  switch (op) {
    case RangeOp::Clear:
      sel.selected.clear();
      return;
    case RangeOp::Toggle: {
      if (!InUniverse(sel, focus.key)) {
        return;
      }
      auto it = sel.selected.find(focus.key);
      if (it == sel.selected.end()) {
        sel.selected.insert(focus.key);
      } else {
        sel.selected.erase(it);
      }
      return;
    }
    case RangeOp::ExtendShift: {
      auto focusIt =
          std::find(sel.universe.begin(), sel.universe.end(), focus.key);
      if (focus.key.empty() || focusIt == sel.universe.end()) {
        return;
      }
      auto anchorIt =
          std::find(sel.universe.begin(), sel.universe.end(), anchor.key);
      if (anchor.key.empty() || anchorIt == sel.universe.end()) {
        // Unknown anchor: select just the known focus key.
        sel.selected.clear();
        sel.selected.insert(focus.key);
        return;
      }
      size_t lo = static_cast<size_t>(anchorIt - sel.universe.begin());
      size_t hi = static_cast<size_t>(focusIt - sel.universe.begin());
      if (lo > hi) {
        std::swap(lo, hi);
      }
      sel.selected.clear();
      for (size_t i = lo; i <= hi; ++i) {
        sel.selected.insert(sel.universe[i]);
      }
      return;
    }
    case RangeOp::AddCtrl:
      if (InUniverse(sel, focus.key)) {
        sel.selected.insert(focus.key);
      }
      return;
    case RangeOp::SelectAll:
      sel.selected.clear();
      for (const auto& key : sel.universe) {
        sel.selected.insert(key);
      }
      return;
  }
}

ActionBarState DeriveActionBar(const SelectionSet& sel) {
  if (sel.selected.empty()) {
    return ActionBarState{false, 0, {}};
  }
  return ActionBarState{true, sel.selected.size(),
                        {"copy", "share", "delete"}};
}

}  // namespace lists

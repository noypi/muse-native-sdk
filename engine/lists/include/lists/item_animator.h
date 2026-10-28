#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "lists/myers_diff.h"
#include "lists/stable_key.h"

namespace lists {

// Visible (realized-window) keys for one animation pass. Entries are the
// stable identity strings from `ItemKey::key`; matching is by key, never by
// index. An empty set means nothing is on screen, so no op animates.
struct KeySet {
  std::vector<std::string> keys;
};

enum class AnimType { Add, Remove, Move };

// One per-item animation op translated from an `EditOp`. `from` is the
// old-list index (Remove/Move), `to` is the new-list index (Add/Move);
// the untouched side carries 0, matching the `EditOp` conventions in
// `lists/myers_diff.h`. No `Change` case exists: the `EditScript`
// alphabet is Insert/Remove/Move only, so same-key content changes have
// no producer through this signature.
struct ItemAnimOp {
  AnimType type = AnimType::Add;
  std::string key;
  size_t from = 0;
  size_t to = 0;
};

// Diff-driven animation script for one batch: one op per visible
// added/removed/moved key, in `EditScript` order (moves, then removals,
// then inserts). Unchanged keys never appear here, so applying the script
// never rebinds them (AC-LISTS-018).
struct ItemAnimScript {
  std::vector<ItemAnimOp> ops;
};

// MOTION-INTERRUPT re-target hook. When a new diff lands while an op is
// still in flight, interruption re-targeting is delegated to the motion
// layer through this callback — this file never reimplements motion
// interpolation. The default (no hook set) keeps the in-flight op as is.
using RetargetHook =
    std::function<ItemAnimOp(const ItemAnimOp& inFlight, const EditOp& incoming)>;

// Installs (or, with ClearRetargetHook, removes) the motion-layer hook.
void SetRetargetHook(RetargetHook hook);
void ClearRetargetHook();

// Applies the installed hook to `inFlight`, or returns it unchanged when
// no hook is set.
ItemAnimOp RetargetForInterrupt(const ItemAnimOp& inFlight,
                                const EditOp& incoming);

// Translates the Myers `EditScript` into per-item anim ops, filtered to
// the visible window: Add/Move ops animate only when their key is in
// `visible`; a Remove animates when its key is in `visible` or its old
// position falls inside the visible window's new-list index span (the
// removed row was on screen before the batch). Off-screen touches yield
// no op. Pure function of its inputs; bounded by the op count.
ItemAnimScript BuildAnimScript(const EditScript& diff, const KeySet& visible);

// True when `k` needs a (re)bind under the script: the key carries an Add
// or Move op. False for unchanged keys (no op), for off-screen keys
// (filtered out), and for removals, which animate out without rebinding —
// matching the `applyBatch` rebind accounting (inserts + moves only).
bool IsRebound(ItemKey k, const ItemAnimScript& s);

}  // namespace lists

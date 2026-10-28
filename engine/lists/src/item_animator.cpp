#include "lists/item_animator.h"

#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace lists {

namespace {

RetargetHook& retargetHook() {
  static RetargetHook hook;
  return hook;
}

}  // namespace

void SetRetargetHook(RetargetHook hook) { retargetHook() = std::move(hook); }

void ClearRetargetHook() { retargetHook() = nullptr; }

ItemAnimOp RetargetForInterrupt(const ItemAnimOp& inFlight,
                                const EditOp& incoming) {
  const RetargetHook& hook = retargetHook();
  if (hook) return hook(inFlight, incoming);
  return inFlight;
}

ItemAnimScript BuildAnimScript(const EditScript& diff, const KeySet& visible) {
  const std::unordered_set<std::string> vis(visible.keys.begin(),
                                            visible.keys.end());
  // New-list positions of every key, so visible keys map to the realized
  // window's index span. Keys are unique per frame (stable-key contract).
  std::unordered_map<std::string, size_t> newPos;
  newPos.reserve(diff.result.size());
  for (size_t i = 0; i < diff.result.size(); ++i) {
    newPos[diff.result[i]] = i;
  }
  bool hasSpan = false;
  size_t lo = 0;
  size_t hi = 0;
  for (const auto& k : visible.keys) {
    const auto it = newPos.find(k);
    if (it == newPos.end()) continue;
    if (!hasSpan) {
      lo = it->second;
      hi = it->second;
      hasSpan = true;
    } else {
      if (it->second < lo) lo = it->second;
      if (it->second > hi) hi = it->second;
    }
  }

  ItemAnimScript out;
  out.ops.reserve(diff.ops.size());
  for (const EditOp& e : diff.ops) {
    ItemAnimOp op;
    op.key = e.key;
    switch (e.type) {
      case EditType::Insert:
        if (vis.find(e.key) == vis.end()) continue;
        op.type = AnimType::Add;
        op.from = 0;
        op.to = e.to;
        break;
      case EditType::Remove:
        // A removed key is already gone from the new list, so membership
        // alone would drop every removal: also keep it when its old
        // position sat inside the realized window's span.
        if (vis.find(e.key) == vis.end() &&
            !(hasSpan && lo <= e.from && e.from <= hi)) {
          continue;
        }
        op.type = AnimType::Remove;
        op.from = e.from;
        op.to = 0;
        break;
      case EditType::Move:
        if (vis.find(e.key) == vis.end()) continue;
        op.type = AnimType::Move;
        op.from = e.from;
        op.to = e.to;
        break;
    }
    out.ops.push_back(op);
  }
  return out;
}

bool IsRebound(ItemKey k, const ItemAnimScript& s) {
  for (const ItemAnimOp& op : s.ops) {
    if (op.key == k.key && op.type != AnimType::Remove) return true;
  }
  return false;
}

}  // namespace lists

#pragma once

#include "layout/solver.h"

namespace layout {

// Zero-cost customization point for subtrees with bespoke geometry
// (slivers, masonry, ...). Implemented as direct virtual dispatch on pure
// C++ types: no marshalling, no JNI, no heap allocation per call.
//
// Lifetime: registration installs the raw pointer; the caller owns the
// delegate and must keep it alive while it is registered. The Solver never
// takes ownership (destroying a Solver with a stack-owned delegate
// installed is safe, and delegates may be deleted through the base
// pointer thanks to the virtual destructor).
class LayoutDelegate {
 public:
  virtual ~LayoutDelegate() = default;
  virtual void measure(NodeId id, float availW, float availH) = 0;
  virtual void arrange(NodeId id, Box bounds) = 0;
  virtual bool handles(NodeId id) const = 0;
};

// Installs `d` for the subtree rooted at `subtreeRoot`, replacing any
// existing registration for that root. Returns true on install, false for
// a nullptr delegate or an unknown root. Never throws.
bool registerDelegate(Solver& s, NodeId subtreeRoot, LayoutDelegate* d) noexcept;

// Removes the registration for `subtreeRoot` if present. Unknown roots are
// ignored. Never throws.
void unregisterDelegate(Solver& s, NodeId subtreeRoot) noexcept;

}  // namespace layout

#pragma once

#include <vector>

#include "lists/collection_semantics.h"
#include "lists/stable_key.h"

namespace lists {

// Log of bind/prefetch arrivals in arrival order. Recycling and prefetch may
// deliver keys in any order (reverse, strided, nested-prefetch interleave);
// the exposed semantics order is the node's logical `items` order, never this
// arrival order.
struct BindLog {
  std::vector<ItemKey> bindSequence;
};

// Returns true iff the exposed semantics order equals the logical index order
// irrespective of bind/prefetch sequence: the bind log must contain exactly
// the node's logical keys, each exactly once (same multiset as
// `CollectionNode::items`). Arrival order is ignored — the semantics snapshot
// is sorted by logical index at publish time, so reverse, strided, and
// prefetch-interleaved sequences all verify. A wrong, missing, extra, or
// duplicated key (or an empty log against a non-empty node) returns false;
// an empty log against an empty node returns true.
bool VerifyLogicalOrder(const CollectionNode& n, const BindLog& binds);

}  // namespace lists

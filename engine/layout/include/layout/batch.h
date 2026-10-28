#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "layout/property.h"
#include "layout/types.h"

namespace layout {

// Observer for subtree invalidations. Installed as a raw pointer via
// `setInvalidationSink` (the caller owns the lifetime); `nullptr`
// restores the counting default (counter still advances, no delivery).
class InvalidationSink {
 public:
  virtual void onInvalidated(NodeId subtreeRoot) = 0;
  virtual ~InvalidationSink() = default;
};

// RAII batch scope over a `PropertyTree`. While at least one scope is
// open, `setBatched` queues one invalidation per touched subtree-root
// instead of invalidating per mutation; nested scopes coalesce to the
// outermost close, which emits the queued invalidations.
class BatchScope {
 public:
  explicit BatchScope(PropertyTree& t);
  ~BatchScope();

  BatchScope(const BatchScope&) = delete;
  BatchScope& operator=(const BatchScope&) = delete;

  std::size_t pendingCount() const noexcept;

 private:
  PropertyTree* tree_;
};

// Batched mutation: applies `PropertyTree::set` immediately, then either
// invalidates at once (no open scope) or queues one invalidation for the
// node's subtree-root (open scope). Never allocates, never throws.
void setBatched(PropertyTree& t, NodeId node, PropHandle h, float v) noexcept;

// Installs the global invalidation observer (`nullptr` restores the
// counting default). Never throws.
void setInvalidationSink(InvalidationSink* s) noexcept;

// Process-global count of emitted invalidations. Tests measure deltas,
// never absolute values. Never throws.
std::uint64_t invalidationCount() noexcept;

}  // namespace layout

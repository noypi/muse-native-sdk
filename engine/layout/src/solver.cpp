#include "layout/solver.h"

#include "layout/delegate.h"
#include "layout/masonry.h"
#include "layout/sliver.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace layout {
namespace {

// Constraint projection sweep cap. Pure equalities converge in one sweep;
// the cap only bounds pathological conflict oscillation (e.g. two pins on
// the same channel with equal weight flipping back and forth).
constexpr int kMaxProjectionIterations = 100;

// Exhaustive backend check with no `default`: adding an enumerator without
// handling it breaks the build under -Werror (-Wswitch).
void validateBackend(Backend b) {
  switch (b) {
    case Backend::Flex:
    case Backend::Constraint:
      return;
  }
  throw std::invalid_argument("unknown Backend value");
}

}  // namespace

NodeId Solver::addNode(Backend b, NodeId parent) {
  validateBackend(b);
  if (parent != kInvalidNodeId &&
      (static_cast<std::size_t>(parent) >= nodes_.size())) {
    throw std::out_of_range("unknown parent id");
  }
  const auto id = static_cast<NodeId>(nodes_.size());
  nodes_.push_back(NodeRec{b, parent, 0.0f, 0.0f, 0.0f});
  solved_ = false;
  return id;
}

void Solver::setFlex(NodeId id, float w, float h, float flexGrow) {
  if (static_cast<std::size_t>(id) >= nodes_.size()) {
    throw std::out_of_range("unknown node id");
  }
  nodes_[static_cast<std::size_t>(id)].w = w;
  nodes_[static_cast<std::size_t>(id)].h = h;
  nodes_[static_cast<std::size_t>(id)].flexGrow = flexGrow;
  solved_ = false;
}

void Solver::addConstraint(const Constraint& c) {
  if (static_cast<std::size_t>(c.node) >= nodes_.size()) {
    throw std::out_of_range("unknown constraint node id");
  }
  constraints_.push_back(c);
  solved_ = false;
}

Box Solver::solve(NodeId root) {
  if (static_cast<std::size_t>(root) >= nodes_.size()) {
    throw std::out_of_range("unknown root id");
  }
  boxes_.clear();
  boxes_.resize(nodes_.size());

  // Mixed tree: flex subtrees first (row main axis, +x), then constraints.
  // The solve target starts at the origin with its own fixed size.
  {
    const NodeRec& rootNode = nodes_.at(static_cast<std::size_t>(root));
    boxes_.at(static_cast<std::size_t>(root)) = Box{0.0f, 0.0f, rootNode.w,
                                                    rootNode.h};
  }
  layoutFlexSubtree(root);

  // Constraint backend: iterative projection of linear pins onto box
  // channels. Sorted by stay weight so stronger pins apply last and win
  // conflicts; std::stable_sort keeps insertion order among equal weights
  // so repeated solves are deterministic.
  std::vector<const Constraint*> ordered;
  ordered.reserve(constraints_.size());
  for (const auto& c : constraints_) {
    if (c.node == root ||
        // Only project constraints for nodes under the solved root.
        // Walk up to check membership in the solved subtree.
        [&] {
          NodeId cur = c.node;
          while (cur != kInvalidNodeId) {
            if (cur == root) return true;
            cur = nodes_[static_cast<std::size_t>(cur)].parent;
          }
          return false;
        }()) {
      ordered.push_back(&c);
    }
  }
  std::stable_sort(ordered.begin(), ordered.end(),
                   [](const Constraint* a, const Constraint* b) {
                     return a->weight < b->weight;
                   });
  for (int iter = 0; iter < kMaxProjectionIterations; ++iter) {
    bool changed = false;
    for (const Constraint* c : ordered) {
      if (isHandled(c->node)) {
        continue;  // Delegated subtrees skip the built-in backends.
      }
      Box& box = boxes_[static_cast<std::size_t>(c->node)];
      float* channel = nullptr;
      switch (c->attribute) {
        case 0: channel = &box.x; break;
        case 1: channel = &box.y; break;
        case 2: channel = &box.w; break;
        case 3: channel = &box.h; break;
        default: break;  // Unknown channels are ignored, never crash.
      }
      if (channel != nullptr && *channel != c->constant) {
        *channel = c->constant;
        changed = true;
      }
    }
    if (!changed) break;
  }

  solved_ = true;
  dispatchDelegates(root);
  return boxes_.at(static_cast<std::size_t>(root));
}

const Box& Solver::boxOf(NodeId id) const {
  if (!solved_) {
    throw std::out_of_range("solve has not been called");
  }
  if (static_cast<std::size_t>(id) >= boxes_.size()) {
    throw std::out_of_range("unknown node id");
  }
  return boxes_.at(static_cast<std::size_t>(id));
}

// Flex backend: row layout on the +x main axis, mirroring the
// engine/runtime/src/layout.cpp algorithm shape (fixed main-axis sizes win,
// the remainder splits proportionally to flexGrow; cross axis keeps the
// child's fixed size; children visited in id order for determinism).
// Constraint-backend children take part as fixed zero-size frames here and
// are pinned to their real frames by the projection pass in solve().
void Solver::layoutFlexSubtree(NodeId id) {
  const Box self = boxes_.at(static_cast<std::size_t>(id));

  // Children in id order (stable input order).
  std::vector<NodeId> children;
  for (NodeId cand = 0; static_cast<std::size_t>(cand) < nodes_.size();
       ++cand) {
    if (nodes_[static_cast<std::size_t>(cand)].parent == id) {
      children.push_back(cand);
    }
  }

  if (children.empty()) {
    return;
  }

  // Fixed main-axis sizes and flex weights (gap/padding/margin are zero on
  // this path; the shape — fixed wins, remainder splits — matches
  // engine/runtime/src/layout.cpp).
  float fixedTotal = 0.0f;
  float flexTotal = 0.0f;
  for (NodeId child : children) {
    const NodeRec& cs = nodes_.at(static_cast<std::size_t>(child));
    if (cs.flexGrow > 0.0f) {
      flexTotal += cs.flexGrow;
    } else {
      fixedTotal += cs.w;
    }
  }

  float remaining = self.w - fixedTotal;
  if (remaining < 0.0f) remaining = 0.0f;

  float cursor = self.x;
  for (NodeId child : children) {
    if (isHandled(child)) {
      continue;  // Delegated subtrees skip the built-in flex backend.
    }
    const NodeRec& cs = nodes_.at(static_cast<std::size_t>(child));
    float main = cs.w;
    if (cs.flexGrow > 0.0f && flexTotal > 0.0f) {
      main = remaining * (cs.flexGrow / flexTotal);
    }
    Box& box = boxes_.at(static_cast<std::size_t>(child));
    box.x = cursor;
    box.y = self.y;
    box.w = main;
    box.h = cs.h;
    cursor += main;
    layoutFlexSubtree(child);
  }
}

// T3 delegate walk: membership test, handled check, and dispatch. All
// dispatch is direct virtual calls on pure C++ types (no marshalling, no
// JNI, no heap allocation on the call path).
bool Solver::isInSubtree(NodeId node, NodeId root) const noexcept {
  NodeId cur = node;
  while (cur != kInvalidNodeId) {
    if (cur == root) {
      return true;
    }
    if (static_cast<std::size_t>(cur) >= nodes_.size()) {
      return false;
    }
    cur = nodes_[static_cast<std::size_t>(cur)].parent;
  }
  return false;
}

bool Solver::isHandled(NodeId id) const {
  for (const auto& entry : delegates_) {
    LayoutDelegate* d = entry.second;
    if (d == nullptr) {
      continue;
    }
    if (id == entry.first || isInSubtree(id, entry.first)) {
      if (d->handles(id)) {
        return true;
      }
    }
  }
  return false;
}

void Solver::dispatchDelegates(NodeId root) {
  for (NodeId id = 0; static_cast<std::size_t>(id) < nodes_.size(); ++id) {
    if (!isInSubtree(id, root) || !isHandled(id)) {
      continue;
    }
    // Available size comes from the parent frame (own frame for parentless
    // roots); values are informational — arrange receives the solved box.
    float availW = boxes_.at(static_cast<std::size_t>(id)).w;
    float availH = boxes_.at(static_cast<std::size_t>(id)).h;
    const NodeId parent = nodes_.at(static_cast<std::size_t>(id)).parent;
    if (parent != kInvalidNodeId &&
        static_cast<std::size_t>(parent) < boxes_.size()) {
      availW = boxes_.at(static_cast<std::size_t>(parent)).w;
      availH = boxes_.at(static_cast<std::size_t>(parent)).h;
    }
    for (const auto& entry : delegates_) {
      LayoutDelegate* d = entry.second;
      if (d == nullptr) {
        continue;
      }
      if ((id == entry.first || isInSubtree(id, entry.first)) &&
          d->handles(id)) {
        // T4 sliver write-back: handled nodes skip the built-in backends, so
        // their boxes are still default-constructed at this point. A
        // SliverDelegate computes exact frames as a pure function of
        // (config, scroll offset); install them before measure/arrange so
        // the bounds forwarded to arrange() equal boxOf(id) (the T3
        // arrange-bounds invariant) and boxOf() reports sliver geometry
        // afterwards. Non-sliver delegates keep the pass-through behavior.
        if (SliverDelegate* sliver = dynamic_cast<SliverDelegate*>(d)) {
          const Box& rootBox =
              boxes_.at(static_cast<std::size_t>(entry.first));
          int childIndex = -1;
          float naturalW = 0.0f;
          float naturalH = 0.0f;
          {
            const NodeRec& rec = nodes_.at(static_cast<std::size_t>(id));
            naturalW = rec.w;
            naturalH = rec.h;
          }
          if (id != entry.first) {
            int pos = 0;
            for (NodeId cand = 0;
                 static_cast<std::size_t>(cand) < nodes_.size(); ++cand) {
              if (nodes_.at(static_cast<std::size_t>(cand)).parent ==
                  entry.first) {
                if (cand == id) {
                  childIndex = pos;
                  break;
                }
                ++pos;
              }
            }
          }
          if (childIndex >= 0 || id == entry.first) {
            Box frame{0.0f, 0.0f, 0.0f, 0.0f};
            if (sliver->frameFor(id, rootBox, childIndex, naturalW, naturalH,
                                 &frame)) {
              boxes_.at(static_cast<std::size_t>(id)) = frame;
            }
          }
        } else if (MasonryDelegate* masonry =
                       dynamic_cast<MasonryDelegate*>(d)) {
          const Box& rootBox =
              boxes_.at(static_cast<std::size_t>(entry.first));
          int childIndex = -1;
          float naturalW = 0.0f;
          float naturalH = 0.0f;
          {
            const NodeRec& rec = nodes_.at(static_cast<std::size_t>(id));
            naturalW = rec.w;
            naturalH = rec.h;
          }
          if (id != entry.first) {
            int pos = 0;
            for (NodeId cand = 0;
                 static_cast<std::size_t>(cand) < nodes_.size(); ++cand) {
              if (nodes_.at(static_cast<std::size_t>(cand)).parent ==
                  entry.first) {
                if (cand == id) {
                  childIndex = pos;
                  break;
                }
                ++pos;
              }
            }
          }
          if (childIndex >= 0 || id == entry.first) {
            Box frame{0.0f, 0.0f, 0.0f, 0.0f};
            if (masonry->frameFor(id, rootBox, childIndex, naturalW, naturalH,
                                  &frame)) {
              boxes_.at(static_cast<std::size_t>(id)) = frame;
            }
          }
        }
        d->measure(id, availW, availH);
        d->arrange(id, boxes_.at(static_cast<std::size_t>(id)));
        break;  // One delegate owns each node: the first match wins.
      }
    }
  }
}

}  // namespace layout

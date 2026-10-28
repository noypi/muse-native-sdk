#include "layout/form_factor.h"

#include <stdexcept>

namespace layout {
namespace {

// Exact field-for-field comparison. Fixture values are exactly
// representable in float, and repeats carry identical structs, so bitwise
// equality is the right no-op detector (no epsilon: near-miss states must
// still count as changes and drive a pass).
bool statesEqual(const FormFactorState& a, const FormFactorState& b) {
  return a.posture == b.posture && a.hasHinge == b.hasHinge &&
         a.hingeX == b.hingeX && a.hingeY == b.hingeY &&
         a.hingeW == b.hingeW && a.hingeH == b.hingeH &&
         a.hingeOccludes == b.hingeOccludes &&
         a.orientation == b.orientation;
}

bool hasUsableHinge(const FormFactorState& s) {
  return s.hasHinge && s.hingeW > 0.0f && s.hingeH > 0.0f;
}

// Nonzero-area overlap between a solved box and the hinge rect. Fixtures
// keep the hinge strictly inside (or strictly outside) child boxes, so
// edge-touch semantics stay unasserted; strict inequalities make edge
// contact report "no intersection".
bool intersectsHinge(const Box& b, const FormFactorState& s) {
  return b.x < s.hingeX + s.hingeW && s.hingeX < b.x + b.w &&
         b.y < s.hingeY + s.hingeH && s.hingeY < b.y + b.h;
}

}  // namespace

void FormFactorSource::onWindowLayoutChanged(
    const FormFactorState& s) noexcept {
  current_ = s;
}

const FormFactorState& FormFactorSource::current() const noexcept {
  return current_;
}

std::vector<NodeId> FormFactorSource::affectedSubtrees(
    const Solver& s, NodeId root) const {
  std::vector<NodeId> out;
  if (!hasUsableHinge(current_)) {
    return out;
  }
  // Node ids are allocated densely from 0 (Solver::addNode appends), so
  // probe from 0 until the first unknown id. The Solver exposes no child
  // enumeration on the T6 surface, so every non-root node whose solved box
  // overlaps the hinge rect is reported; for the flat two-level fixtures
  // that is exactly the set of intersecting direct children. On an
  // unsolved solver boxOf throws and the set is empty.
  for (NodeId id = 0;; ++id) {
    const Box* box = nullptr;
    try {
      box = &s.boxOf(id);
    } catch (const std::out_of_range&) {
      break;
    }
    if (id == root) {
      continue;
    }
    if (intersectsHinge(*box, current_)) {
      out.push_back(id);
    }
    // NodeId is uint32: stop before wrap-around on pathological sizes.
    if (id == kInvalidNodeId - 1) {
      break;
    }
  }
  return out;
}

bool FormFactorSource::consumeForNextPass(Solver& s, NodeId root) {
  if (hasApplied_ && statesEqual(current_, applied_)) {
    return false;
  }
  s.solve(root);
  // FULL-occlusion hinge: move affected subtrees off the hinge rect via
  // constraint pins (public Solver API; the pins apply inside the second
  // solve without shifting unaffected siblings, whose flex positions come
  // from the fixed input sizes). Non-occluding or absent hinges keep the
  // full-width pass from the solve above.
  if (hasUsableHinge(current_) && current_.hingeOccludes) {
    const std::vector<NodeId> affected = affectedSubtrees(s, root);
    for (const NodeId id : affected) {
      Box b;
      try {
        b = s.boxOf(id);
      } catch (const std::out_of_range&) {
        continue;
      }
      if (!intersectsHinge(b, current_)) {
        continue;
      }
      Constraint pin;
      pin.node = id;
      pin.weight = 1.0f;
      if (b.x < current_.hingeX) {
        // Left segment ends at the hinge's left edge (edge contact is not
        // an intersection, so content sits exactly up against the hinge).
        float w = current_.hingeX - b.x;
        if (w < 0.0f) {
          w = 0.0f;
        }
        pin.attribute = 2;  // w
        pin.constant = w;
      } else {
        // Starts at/inside the hinge: shift past its right edge.
        pin.attribute = 0;  // x
        pin.constant = current_.hingeX + current_.hingeW;
      }
      try {
        s.addConstraint(pin);
      } catch (const std::out_of_range&) {
        continue;
      }
    }
    if (!affected.empty()) {
      s.solve(root);
    }
  }
  applied_ = current_;
  hasApplied_ = true;
  return true;
}

}  // namespace layout

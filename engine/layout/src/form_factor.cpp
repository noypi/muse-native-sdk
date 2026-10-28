#include "layout/form_factor.h"

#include <stdexcept>
#include <utility>
#include <vector>

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

// Hinge-pin lifetime: pins appended via Solver::addConstraint persist in
// the Solver's constraint list, so each pass first truncates the trailing
// run pinned by the previous pass (snapshot/restore of the constraint
// count). Without this a removed hinge would keep shifting/shrinking
// leaves after unfold-back, and repeated toggles would grow the list
// without bound. The snapshot mark is stored in the Solver itself (see
// Solver::setHingePins), so lifetime follows the solver: destroying a
// solver while pins are live cannot leak a slot or truncate another
// solver's constraints on address reuse.

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
  // Drop this solver's hinge pins from the previous pass before solving:
  // Solver::solve projects every accumulated pin, so stale pins would
  // otherwise keep the post-fold frames after the hinge is gone.
  if (s.hasLiveHingePins()) {
    s.truncateConstraints(s.hingePinMark());
    s.clearHingePins();
  }
  s.solve(root);
  // FULL-occlusion hinge: move affected subtrees off the hinge rect via
  // constraint pins (public Solver API; the pins apply inside the second
  // solve without shifting unaffected siblings, whose flex positions come
  // from the fixed input sizes). Non-occluding or absent hinges keep the
  // full-width pass from the solve above.
  if (hasUsableHinge(current_) && current_.hingeOccludes) {
    const std::size_t pinMark = s.constraintCount();
    const std::vector<NodeId> affected = affectedSubtrees(s, root);
    // Snapshot affected boxes before pinning: each addConstraint flips the
    // solver back to unsolved (boxOf throws while unsolved), so a second
    // affected leaf's box would be unreadable once the first pin lands.
    // Collect (id, box) pairs up front, then pin from the snapshots.
    std::vector<std::pair<NodeId, Box>> targets;
    targets.reserve(affected.size());
    for (const NodeId id : affected) {
      try {
        targets.emplace_back(id, s.boxOf(id));
      } catch (const std::out_of_range&) {
        continue;
      }
    }
    for (const auto& target : targets) {
      const NodeId id = target.first;
      const Box b = target.second;
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
      // The pins above form a trailing run starting at pinMark: remember
      // it so the next pass truncates exactly these pins before solving.
      s.setHingePins(pinMark);
    }
  }
  applied_ = current_;
  hasApplied_ = true;
  return true;
}

}  // namespace layout

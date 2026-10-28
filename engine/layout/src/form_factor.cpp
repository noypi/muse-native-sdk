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

bool foldingStatesEqual(const FoldingFeatureState& a,
                        const FoldingFeatureState& b) noexcept {
  return a.state == b.state && a.orientation == b.orientation &&
         a.occlusion == b.occlusion &&
         a.hingeAngleDeg == b.hingeAngleDeg &&
         a.hasHingeAngle == b.hasHingeAngle && a.bounds.x == b.bounds.x &&
         a.bounds.y == b.bounds.y && a.bounds.w == b.bounds.w &&
         a.bounds.h == b.bounds.h;
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

// Folding-driven hinge region: the FoldingFeature bounds rect under
// FULL occlusion. Compat onWindowLayoutChanged state degrades to
// full-width when absent, so an empty rect (or non-FULL occlusion) means
// "no folding hinge".
bool foldingHasHinge(const FormFactorCombined& c) noexcept {
  return c.occlusion == OcclusionType::Full && c.bounds.w > 0.0f &&
         c.bounds.h > 0.0f;
}

bool combinedEqual(const FormFactorCombined& a,
                   const FormFactorCombined& b) noexcept {
  if (!statesEqual(a.compat, b.compat)) {
    return false;
  }
  FoldingFeatureState fa;
  fa.state = a.state;
  fa.orientation = a.orientation;
  fa.occlusion = a.occlusion;
  fa.hingeAngleDeg = a.hingeAngleDeg;
  fa.hasHingeAngle = a.hasHingeAngle;
  fa.bounds = a.bounds;
  FoldingFeatureState fb;
  fb.state = b.state;
  fb.orientation = b.orientation;
  fb.occlusion = b.occlusion;
  fb.hingeAngleDeg = b.hingeAngleDeg;
  fb.hasHingeAngle = b.hasHingeAngle;
  fb.bounds = b.bounds;
  return foldingStatesEqual(fa, fb);
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
  current_.compat = s;
}

void FormFactorSource::onFoldingFeatureChanged(
    FoldingFeatureState s) noexcept {
  FoldingFeatureState cur;
  cur.state = current_.state;
  cur.orientation = current_.orientation;
  cur.occlusion = current_.occlusion;
  cur.hingeAngleDeg = current_.hingeAngleDeg;
  cur.hasHingeAngle = current_.hasHingeAngle;
  cur.bounds = current_.bounds;
  if (foldingStatesEqual(cur, s)) {
    return;
  }
  current_.state = s.state;
  current_.orientation = s.orientation;
  current_.occlusion = s.occlusion;
  current_.hingeAngleDeg = s.hingeAngleDeg;
  current_.hasHingeAngle = s.hasHingeAngle;
  current_.bounds = s.bounds;
}

const FormFactorCombined& FormFactorSource::current() const noexcept {
  return current_;
}

std::vector<NodeId> FormFactorSource::affectedSubtrees(
    const Solver& s, NodeId root) const {
  std::vector<NodeId> out;
  const FormFactorState& ff = current_.compat;
  const bool useCompat = hasUsableHinge(ff);
  const bool useFolding = foldingHasHinge(current_);
  if (!useCompat && !useFolding) {
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
    // Edge-touch counts as affected here (inclusive): after the fold
    // pass the pinned column sits exactly up against the hinge rect
    // (edge contact reports "no intersection" for the no-content
    // property), yet the column is still the hinge-affected subtree.
    // Fixtures keep the unpinned hinge strictly inside/outside child
    // boxes, so inclusivity only matters post-pin; edge-touch semantics
    // otherwise stay unasserted per contract.
    auto touchesCompat = [&]() {
      return box->x <= ff.hingeX + ff.hingeW && ff.hingeX <= box->x + box->w &&
             box->y <= ff.hingeY + ff.hingeH && ff.hingeY <= box->y + box->h;
    };
    auto touchesFolding = [&]() {
      const Rect& r = current_.bounds;
      return box->x <= r.x + r.w && r.x <= box->x + box->w &&
             box->y <= r.y + r.h && r.y <= box->y + box->h;
    };
    const bool hitCompat = useCompat && touchesCompat();
    const bool hitFolding = useFolding && touchesFolding();
    if (hitCompat || hitFolding) {
      out.push_back(id);
    }
    // NodeId is uint32: stop before wrap-around on pathological sizes.
    if (id == kInvalidNodeId - 1) {
      break;
    }
  }
  return out;
}

bool FormFactorSource::consumeForNextPass(Solver& s, NodeId root) noexcept {
  try {
    if (hasApplied_ && combinedEqual(current_, applied_)) {
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
    // from the fixed input sizes). The folding FoldingFeature bounds under
    // FULL occlusion feed this path (bridged here); otherwise the compat
    // hinge applies. Non-occluding or absent hinges keep the full-width
    // pass from the solve above.
    const bool useFolding = foldingHasHinge(current_);
    const FormFactorState& curFF = current_.compat;
    const bool useCompat =
        !useFolding && hasUsableHinge(curFF) && curFF.hingeOccludes;
    float hx = 0.0f;
    float hy = 0.0f;
    float hw = 0.0f;
    float hh = 0.0f;
    if (useFolding) {
      hx = current_.bounds.x;
      hy = current_.bounds.y;
      hw = current_.bounds.w;
      hh = current_.bounds.h;
    } else if (useCompat) {
      hx = curFF.hingeX;
      hy = curFF.hingeY;
      hw = curFF.hingeW;
      hh = curFF.hingeH;
    }
    if (useFolding || useCompat) {
      const FormFactorState hinge{Posture::Unknown, true, hx, hy, hw, hh,
                                  true, 0};
      const std::size_t pinMark = s.constraintCount();
      const std::vector<NodeId> affected = affectedSubtrees(s, root);
      // Snapshot affected boxes before pinning: each addConstraint flips
      // the solver back to unsolved (boxOf throws while unsolved), so a
      // second affected leaf's box would be unreadable once the first pin
      // lands. Collect (id, box) pairs up front, then pin from snapshots.
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
        if (!intersectsHinge(b, hinge)) {
          continue;
        }
        Constraint pin;
        pin.node = id;
        pin.weight = 1.0f;
        if (b.x < hx) {
          // Left segment ends at the hinge's left edge (edge contact is
          // not an intersection, so content sits exactly up against the
          // hinge).
          float w = hx - b.x;
          if (w < 0.0f) {
            w = 0.0f;
          }
          pin.attribute = 2;  // w
          pin.constant = w;
        } else {
          // Starts at/inside the hinge: shift past its right edge.
          pin.attribute = 0;  // x
          pin.constant = hx + hw;
        }
        try {
          s.addConstraint(pin);
        } catch (const std::out_of_range&) {
          continue;
        }
      }
      if (!affected.empty()) {
        s.solve(root);
        // The pins above form a trailing run starting at pinMark:
        // remember it so the next pass truncates exactly these pins
        // before solving.
        s.setHingePins(pinMark);
      }
    }
    applied_ = current_;
    hasApplied_ = true;
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace layout

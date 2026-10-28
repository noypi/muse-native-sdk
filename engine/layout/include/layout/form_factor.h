#pragma once

#include <vector>

#include "layout/solver.h"
#include "layout/types.h"

namespace layout {

// Posture of a foldable device, mirroring the FoldingFeature state values
// carried over the JNI seam owned outside this run. This header only defines
// the C++-side shape; signal emission stays in FR-014.
enum class Posture { Flat, HalfOpened, Unknown };

// C++-side mirror of the androidx.window.layout WindowInfoTracker /
// FoldingFeature input: posture state, orientation
// (opaque int from FoldingFeature#getOrientation), occlusion type mapped to
// `hingeOccludes`, and the hinge/fold bounds rect. A hinge-absent state
// (hasHinge == false, or an empty rect) degrades to full-width: no split.
struct FormFactorState {
  Posture posture = Posture::Unknown;
  bool hasHinge = false;
  float hingeX = 0.0f;
  float hingeY = 0.0f;
  float hingeW = 0.0f;
  float hingeH = 0.0f;
  bool hingeOccludes = false;
  int orientation = 0;
};

// Consumes window-layout updates and applies them on the next solver pass.
// `onWindowLayoutChanged` is the callback entry for the JNI seam: it never
// throws and never calls into JS. `consumeForNextPass` returns false when
// the state is unchanged since the last pass (no-op).
class FormFactorSource {
 public:
  FormFactorSource() = default;

  void onWindowLayoutChanged(const FormFactorState& s) noexcept;
  const FormFactorState& current() const noexcept;

  // Returns the subtree roots intersecting the hinge rect with nonzero-area
  // overlap. With no hinge (or an empty rect) the set is empty: the layout
  // degrades to full-width. Call after `Solver::solve(root)`; on an
  // unsolved solver the set is empty.
  std::vector<NodeId> affectedSubtrees(const Solver& s, NodeId root) const;

  // Applies the pending state to the next pass: re-solves `root` and, for a
  // FULL-occlusion hinge, shrinks/shifts hinge-affected subtrees so no
  // solved box keeps content under the hinge rect. Unaffected subtrees keep
  // their exact frames. Returns false (without re-solving) when the state
  // is unchanged since the last pass.
  bool consumeForNextPass(Solver& s, NodeId root);

 private:
  FormFactorState current_{};
  FormFactorState applied_{};
  bool hasApplied_ = false;
};

}  // namespace layout

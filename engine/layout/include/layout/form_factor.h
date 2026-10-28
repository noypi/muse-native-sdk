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
// Plain viewport/hinge rect (x/y/w/h floats). Aggregate: `Rect{a, b, c, d}`
// maps positionally to x/y/w/h.
struct Rect {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

// Foldable posture input mirroring androidx.window.layout FoldingFeature.
// PascalCase enumerators match the existing `Posture` convention.
enum class FoldState { Flat, HalfOpened, Unknown };
enum class FoldOrientation { Vertical, Horizontal };
enum class OcclusionType { None, Full };

// C++-side mirror of a WindowManager/FoldingFeature update plus the optional
// hinge-angle sensor reading. When the sensor is absent (`hasHingeAngle ==
// false`) layout degrades to `bounds` + `occlusion` only.
struct FoldingFeatureState {
  FoldState state = FoldState::Unknown;
  FoldOrientation orientation = FoldOrientation::Vertical;
  OcclusionType occlusion = OcclusionType::None;
  float hingeAngleDeg = 0.0f;
  bool hasHingeAngle = false;
  Rect bounds{};
};

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

// Combined snapshot returned by `FormFactorSource::current()`. Carries the
// compat `FormFactorState` (as `compat`) alongside the T1 folding fields
// (exposed directly as `state`/`orientation`/`occlusion`/`hingeAngleDeg`/
// `hasHingeAngle`/`bounds`). Implicit conversions to either input struct keep
// both existing `const FormFactorState& cur = src.current();` call sites and
// new `const FoldingFeatureState& cur = src.current();` call sites compiling;
// direct `src.current().orientation` resolves to the folding orientation (the
// compat orientation stays reachable via the conversion).
struct FormFactorCombined {
  FormFactorState compat{};
  FoldState state = FoldState::Unknown;
  FoldOrientation orientation = FoldOrientation::Vertical;
  OcclusionType occlusion = OcclusionType::None;
  float hingeAngleDeg = 0.0f;
  bool hasHingeAngle = false;
  Rect bounds{};

  operator FormFactorState() const noexcept { return compat; }
  operator FoldingFeatureState() const noexcept {
    FoldingFeatureState out;
    out.state = state;
    out.orientation = orientation;
    out.occlusion = occlusion;
    out.hingeAngleDeg = hingeAngleDeg;
    out.hasHingeAngle = hasHingeAngle;
    out.bounds = bounds;
    return out;
  }
};

// Consumes window-layout updates and applies them on the next solver pass.
// `onWindowLayoutChanged` is the callback entry for the JNI seam: it never
// throws and never calls into JS. `consumeForNextPass` returns false when
// the state is unchanged since the last pass (no-op).
class FormFactorSource {
 public:
  FormFactorSource() = default;

  void onWindowLayoutChanged(const FormFactorState& s) noexcept;
  void onFoldingFeatureChanged(FoldingFeatureState s) noexcept;
  const FormFactorCombined& current() const noexcept;

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
  FormFactorCombined current_{};
  FormFactorCombined applied_{};
  bool hasApplied_ = false;
};

}  // namespace layout

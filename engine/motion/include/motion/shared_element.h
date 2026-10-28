#ifndef MOTION_SHARED_ELEMENT_H_
#define MOTION_SHARED_ELEMENT_H_

#include <string>
#include <vector>

#include "motion/graph.h"
#include "motion/implicit.h"
#include "motion/layout_adapter.h"

namespace motion {

struct SharedAnchor {
  std::string elementId;
  std::string route;
  int depth = 0;
  Box box;
  // Clip rect riding the same t01 as the box. Defaults to zeros so existing
  // call sites compile unchanged; legacy clipX/clipY on SharedFrame mirror
  // clip.x/clip.y.
  Box clip;
  float cornerRadius = 0.0f;
};

const SharedAnchor* motion_matchShared(const std::vector<SharedAnchor>& from,
                                       const std::vector<SharedAnchor>& to,
                                       const std::string& elementId) noexcept;

struct SharedFrame {
  Box box;
  // Real clip rect, lerped with the same t01 as the box. Legacy scalar
  // channels mirror clip.x/clip.y for compat; prefer `clip`.
  Box clip;
  float clipX = 0.0f;
  float clipY = 0.0f;
  float cornerRadius = 0.0f;
  float t01 = 0.0f;
};

SharedFrame motion_interpolateShared(const SharedAnchor& from,
                                     const SharedAnchor& to,
                                     float t01) noexcept;

// T5: registers a two-anchor transition node in the caller's Graph and
// arms the global back-progress scrub state. The node reuses existing
// composite shapes (sequence of two value leaves); no new physics.
NodeId motion_sharedTransition(Graph& g, const SharedAnchor& from,
                               const SharedAnchor& to,
                               const TransitionSpec& spec);

// T5: absorbs a back-progress sample in [0,1] (out-of-range clamps) and
// returns the current scrub t01. Continuity comes from the existing solver
// retarget (value+velocity carry over); noexcept and deterministic.
// Single-active-transition: scrub state is process-wide file-global armed by
// the latest motion_sharedTransition; concurrent transitions interfere.
// Tests use motion_resetSharedDrive() to clear the armed state.
float motion_driveShared(float backProgress01) noexcept;

// Test-only reset: clears the armed scrub state so cases do not leak into
// each other. Production behavior unchanged.
void motion_resetSharedDrive() noexcept;

// T6: wraps an injected platform-view surface rect as a matchable anchor;
// matching reuses motion_matchShared.
SharedAnchor motion_pviewAnchor(const std::string& elementId,
                                const std::string& route,
                                const Box& surfaceRect,
                                float cornerRadius) noexcept;

}  // namespace motion

#endif  // MOTION_SHARED_ELEMENT_H_

#ifndef MOTION_LARGE_MOTION_H_
#define MOTION_LARGE_MOTION_H_

#include <vector>

#include "motion/layout_adapter.h"

namespace motion {

// T7: posture/window-size-aware variants + hinge-occlusion continuation.
enum class Posture { Flat, Tabletop, HalfOpened, Folded };
enum class HingeOcclusion { None, Partial, Full };
enum class SizeClass { Compact, Medium, Expanded };

struct PaneMove {
  Box fromPane;
  Box toPane;
  bool crossHinge = false;
};

PaneMove motion_paneMove(Posture p, HingeOcclusion occ, const Box& a,
                         const Box& b) noexcept;

float motion_scaledDuration(float baseMs, SizeClass sc, float minMs,
                            float maxMs) noexcept;

Box motion_containerTransform(const std::vector<Box>& children, const Box& from,
                              const Box& to, float t01) noexcept;

}  // namespace motion

#endif  // MOTION_LARGE_MOTION_H_

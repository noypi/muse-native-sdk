#include "motion/reduced_motion.h"

#include <atomic>

namespace motion {

namespace {

// Process-global toggle; atomic so a settings/UI thread can flip it while
// the render thread reads activeVariant without a data race.
std::atomic<bool> gReducedMotion{false};

const char* fullName(PatternId p) {
  switch (p) {
    case PatternId::FadeThrough:
      return "fade-through";
    case PatternId::SharedAxisX:
      return "shared-axis-x";
    case PatternId::SharedAxisY:
      return "shared-axis-y";
    case PatternId::SharedAxisZ:
      return "shared-axis-z";
    case PatternId::ContainerTransform:
      return "container-transform";
  }
  return "fade-through";
}

// Slide/scale motion is replaced, never silently dropped: every entry is a
// perceptible opacity/duration change that still drives the enter/exit
// flight through the T4 evaluators.
const char* reducedName(PatternId p) {
  switch (p) {
    case PatternId::FadeThrough:
      return "cross-fade";
    case PatternId::SharedAxisX:
      return "shorten-x";
    case PatternId::SharedAxisY:
      return "shorten-y";
    case PatternId::SharedAxisZ:
      return "shorten-z";
    case PatternId::ContainerTransform:
      return "cross-fade";
  }
  return "cross-fade";
}

}  // namespace

void motion_setReducedMotion(bool enabled) { gReducedMotion = enabled; }

const char* activeVariant(PatternId p) {
  return gReducedMotion ? reducedName(p) : fullName(p);
}

bool motion_lastBudgetShed() { return false; }

}  // namespace motion

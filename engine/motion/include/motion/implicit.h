#ifndef MOTION_IMPLICIT_H_
#define MOTION_IMPLICIT_H_

#include <cstdint>
#include <unordered_map>

#include "motion/solver.h"

namespace motion {

struct PropertyId {
  std::uint32_t value = 0;
};

struct TransitionSpec {
  SpringSpec spring;
  float delayMs = 0.0f;
  float staggerMs = 0.0f;
};

// Implicit transition registry (FR-MOTION-007 / AC-MOTION-014, AC-MOTION-015).
//
// A marked property animates toward each new target via the T1 spring solver
// with no caller animation code; an unmarked property snaps. Interrupting an
// in-flight transition with a new set retargets from the current
// value+velocity, preserving continuity.
class ImplicitRegistry {
 public:
  void markImplicit(PropertyId p, TransitionSpec s);
  void set(PropertyId p, float target, float tMs);
  float get(PropertyId p, float tMs) const noexcept;

 private:
  struct Entry {
    bool implicit = false;
    TransitionSpec spec;
    bool hasValue = false;
    bool active = false;
    float target = 0.0f;
    SpringState state;
    float stateMs = 0.0f;
    float startMs = 0.0f;
  };

  std::unordered_map<std::uint32_t, Entry> entries_;

  SpringState evaluate(const Entry& e, float tMs) const noexcept;
};

}  // namespace motion

#endif  // MOTION_IMPLICIT_H_

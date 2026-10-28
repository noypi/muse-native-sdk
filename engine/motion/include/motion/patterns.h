#pragma once

// Canonical M3 transition patterns (FR-MOTION-014 / AC-045).
// Each pattern drives enter/exit for a target with no app-side per-frame
// code; time comes only from the injected nowMs clock, shared with T2/T3.
// Every pattern resolves its curve/duration/spring from T1 tokens by name
// (resolveToken) — no hardcoded motion constants. Re-entrant
// runEnter/runExit/retarget mid-flight never snaps: the call carries the
// solver forward to nowMs and re-seeds from the carried value+velocity.
//
// The T5 (AC-046) retarget/patternVelocity surface is declared here on the
// same PatternId type so the shared patterns_test.cpp file compiles for
// both items; T6 consumes PatternId via this header for its variant table.

#include <cstdint>
#include <exception>
#include <string>

#include "motion/implicit.h"  // Reuses the T2-committed NodeId handle.

namespace motion {

enum class PatternId {
  FadeThrough,
  SharedAxisX,
  SharedAxisY,
  SharedAxisZ,
  ContainerTransform,
};

// Thrown by patternNamed; what() always names the offending name without
// carrying secrets.
class PatternError : public std::exception {
 public:
  PatternError(std::string name, std::string reason);
  const char* what() const noexcept override;
  std::string Name;
  std::string Reason;

 private:
  std::string message_;
};

// Resolves one of fade-through, shared-axis-x, shared-axis-y,
// shared-axis-z, container-transform. Throws PatternError naming null,
// empty, or unknown names.
PatternId patternNamed(const char* name);

// Drives the enter (0 -> 1) or exit (current -> 0) flight for target.
// Re-entrant calls carry the in-flight solver to nowMs first, so sampling
// immediately after re-entry reads the pre-call value (no snap).
void runEnter(PatternId p, NodeId target, uint64_t nowMs);
void runExit(PatternId p, NodeId target, uint64_t nowMs);

// True while the (pattern, target) pair has an in-flight or freshly
// started flight. Never-run pairs report false, so per-target and
// per-pattern isolation hold.
bool patternActive(PatternId p, NodeId target);

// Retargets the in-flight solver toward toValue, preserving value and
// (with a 0.0f hint) velocity within 1e-3. A nonzero velocityHint seeds
// the solver velocity (gesture/release velocity carried in as a value).
void retarget(PatternId p, NodeId target, float toValue, float velocityHint,
              uint64_t nowMs);

// Reports the last carried value+velocity. Null out-pointers are ignored
// (never a crash). Has no clock parameter: time advances only through the
// nowMs-carrying calls above.
void patternVelocity(PatternId p, NodeId target, float* value,
                     float* velocity);

}  // namespace motion

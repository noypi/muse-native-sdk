#ifndef MOTION_IMPLICIT_H_
#define MOTION_IMPLICIT_H_

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "motion/graph.h"
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

// A single item move inside a layout flow (FR-MOTION-007 / AC-MOTION-017).
//
// Reorder/insert is `fromIndex -> toIndex`; removal sets `removed` and fades
// the item through the opacity channel instead of vanishing it instantly.
struct ItemMove {
  PropertyId p;
  float fromIndex = 0.0f;
  float toIndex = 0.0f;
  bool removed = false;
};

// Implicit transition registry (FR-MOTION-007 / AC-MOTION-014, AC-MOTION-015).
//
// A marked property animates toward each new target via the T1 spring solver
// with no caller animation code; an unmarked property snaps. Interrupting an
// in-flight transition with a new set retargets from the current
// value+velocity, preserving continuity.
//
// T8 flows (FR-MOTION-007 / AC-MOTION-017) build on this registry:
// reorder/insert/remove (applyLayoutFlow), adaptive compact<->expanded as a
// position+size target swap through the same path, and theme color/elevation
// (applyTheme) all animate through get() with no app-side code. Removed items
// fade via the opacity channel (getOpacity).
class ImplicitRegistry {
 public:
  void markImplicit(PropertyId p, TransitionSpec s);
  // Guard: non-finite target/tMs are ignored (no state change). Integration
  // per call is capped at 100000 fixed steps; non-finite tMs in get/evaluate
  // returns the current value unchanged.
  void set(PropertyId p, float target, float tMs);
  float get(PropertyId p, float tMs) const noexcept;
  // Opacity channel: 1.0 for live items; 1.0 -> 0.0 for removed items.
  float getOpacity(PropertyId p, float tMs) const noexcept;
  // List reorder/insert/remove flow. Moves naming an unknown PropertyId
  // (no registry entry) are skipped; the rest still apply, no throw.
  void applyLayoutFlow(std::vector<ItemMove> moves, float tMs);
  // Theme toggle: color channel animates toward colorTo, elevation channel
  // toward elevTo. The color/elevation sources retarget from their current
  // values, so the from endpoints only document the previous theme.
  // Routing rule: color targets the lowest implicit PropertyId holding a
  // value and elevation targets the highest implicit PropertyId holding a
  // value; middle ids are untouched. With a single entry only color is
  // applied (elevation is dropped); with zero entries this is a no-op.
  void applyTheme(float colorFrom, float colorTo, float elevFrom, float elevTo,
                  float tMs);

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

  // Removal fades: each removed item animates 1.0 -> 0.0 with its own
  // entry's transition spec so the fade matches the motion feel.
  struct OpacityFade {
    bool removed = false;
    TransitionSpec spec;
    bool hasSpec = false;
    SpringState state;
    float startMs = 0.0f;
  };

  std::unordered_map<std::uint32_t, OpacityFade> opacity_;

  SpringState evaluate(const Entry& e, float tMs) const noexcept;
};

// ---- Declarative implicit transitions (FR-MOTION-002 / AC-006, T2) ----
//
// A restyle/reparent arrives as onPropertyChange; registered properties
// animate from the old value to the new one with no per-frame app code.
// Time comes only from the caller-injected nowMs (milliseconds); the OS
// clock is never read here. Production call sites bridge main's VirtualClock
// by passing motion_nowNanos() / 1000000. Springs evaluate through the T1
// solver (advanceSpring over a SpringSpec converted from the resolved
// token); duration/easing tokens fall back to a smoothstep segment, so the
// token class selects the interpolation path (class-aware fallback).
//
// NodeId is main's graph handle (motion/graph.h): the implicit layer
// addresses graph nodes, it does not fork the id type.

// Registers property+token for implicit animation; throws TokenError naming
// the token when it is null or unknown (resolved via T1 resolveToken).
void markImplicit(NodeId id, const char* property, const char* token);

// Starts (or mid-flight retargets) the transition toward toValue. No-op when
// toValue equals the current target; never crashes on null property names.
void onPropertyChange(NodeId id, const char* property, float toValue,
                      uint64_t nowMs);

// Current interpolated value under the injected clock. Unregistered pairs
// sample to 0.0f; sampling at or before the start clamps to the start value.
float sample(NodeId id, const char* property, uint64_t nowMs);

}  // namespace motion

#endif  // MOTION_IMPLICIT_H_

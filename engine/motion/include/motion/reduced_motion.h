#pragma once

// Reduced-motion variant substitution (FR-MOTION-014 / AC-047).
// Consumes the OS accessibility reduced-motion setting as the boolean input
// to motion_setReducedMotion (no OS API is read; no global duration-scale
// plumbing). Decorative slide/scale motion is replaced by declared
// cross-fade/shorten variants, but essential state-change feedback stays
// perceptible: the reduced variant still drives the same enter/exit flight,
// so patternActive keeps reporting true after runEnter/runExit.

#include "motion/patterns.h"  // Reuses the T4 PatternId registry.

namespace motion {

// Selects the variant table. Process-global; takes effect on the next
// activeVariant call without restart.
void motion_setReducedMotion(bool enabled);

// Returns the full pattern name when reduced motion is off, or the declared
// per-pattern variant (e.g. cross-fade for fade-through) when on. The
// returned pointer refers to static storage and is never null.
const char* activeVariant(PatternId p);

// False on the nominal path in both modes: variant substitution is a
// declared replacement, never silent budget shedding.
bool motion_lastBudgetShed();

}  // namespace motion

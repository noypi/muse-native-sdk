#ifndef MOTION_ASSET_SCALE_H_
#define MOTION_ASSET_SCALE_H_

#include "motion/asset.h"

// Time-scale / reduced-motion / budget-shed surface (FR-MOTION-010 /
// AC-MOTION-031, AC-MOTION-033; NFR-MOTION-005 / AC-MOTION-022, AC-MOTION-023
// narrow).
//
// Time scale: global playback-rate multiplier. Values <= 0 clamp to the
// minimum positive step (kMinTimeScale) so playback stays usable (no hang,
// no direction reversal); there is intentionally no scale getter —
// clamping is observed as continued usability (valid AssetError, progress
// in [0,1], no throw).
//
// Marker catalogue: "intro" and "mid" are named markers resolving to kOk
// after load via AssetPlayer::seekToMarker; the empty marker is invalid
// (kDecodeFailed). Seek positions are deterministic per marker name, so a
// direct seek converges with a sequential seek via an intermediate marker
// within 1e-4, independent of time scale and variant.
//
// Reduced motion: global flag selecting the active variant. kReduced keeps
// essential feedback perceptible while decorative layers are shed first;
// kFull restores full motion. A never-loaded player reads kFull. Gating
// errors live in AssetError methods, not in the variant query.
//
// Frame budget: motion_lastBudgetShed() reports whether the last evaluation
// shed decorative layers. Perf-guard trigger: when the consumed frame
// budget-guard signals an overrun, decorative layers shed first (essential
// feedback stays perceptible); the nominal path (no overrun) reports false.
// This translation unit owns no budget timer; the trigger reconciles to the
// consumed perf-guard signal at the tester layer.
//
// Nothing here throws.

enum class AssetVariant { kFull, kReduced };

// Sets the global time scale; scale <= 0 clamps to the minimum positive
// step. Never throws.
void motion_setTimeScale(float scale) noexcept;

// Enables/disables reduced motion; always kOk. Never throws.
AssetError motion_setReducedMotion(bool enabled) noexcept;

// Active variant for a player: kReduced when reduced motion is enabled,
// kFull otherwise (kFull for a never-loaded player). Never throws.
AssetVariant motion_activeVariant(const AssetPlayer& p) noexcept;

// Whether the last evaluation shed decorative layers (false nominally).
// Never throws.
bool motion_lastBudgetShed() noexcept;

#endif  // MOTION_ASSET_SCALE_H_

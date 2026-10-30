#pragma once

namespace runtime {

// Touch input slop (FR-INPUT-011 / AC-INPUT-020, hermetic half).
//
// FR-INPUT-011 Notes: the node-1 device-path double lived here
// (`dispatch_touch` 100x100 rect + `button_fire_count` increment,
// `handler_fire_count`, `list_scroll_offset`) and was deleted by
// BUG-M3-FIDELITY-007 T6 — it must never coexist with the device chain.
// Tap routing is asserted via `ResolveTapTarget` + `ClaimTap` (T4) and the
// `FireTapHandler`/`RepaintWithinOneFrame` halves (T5); per-host counters
// survive nowhere. This header keeps exactly one production claim: the
// touch-slop source of truth both the device chain and the narrowed
// hermetic slop units share (plan A2).
namespace touch_detail {

// Touch-slop sits between the contract boundary values: a 1px drag is
// noise, a 48px drag is a scroll.
constexpr float kTouchSlopPx = 8.0f;

}  // namespace touch_detail

}  // namespace runtime

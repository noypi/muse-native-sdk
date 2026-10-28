#pragma once

#include <cstdint>

namespace lists {

// Scroll offset as an FR-STATE-001 signal/atom consumed by the render
// graph. The offset is render-graph state: every setOffset is observable
// via offset(), while layoutVersion identifies the realized-window layout
// and is never bumped by a pure offset change.
struct ScrollSignal {
  float offsetPx = 0.0f;
  std::uint64_t layoutVersion = 0;
};

// Stores `px` exactly (negative overscroll and large offsets included;
// clamping is the physics layer's job, not the signal's) and notifies
// render-graph subscribers. Never bumps layoutVersion: unaffected items
// are not relaid out.
void setOffset(ScrollSignal& sig, float px);

// Reads back the last stored offset exactly.
float offset(const ScrollSignal& sig);

// Layout version for items outside the viewport sweep. Stable across
// every offset change, including same-value writes.
std::uint64_t layoutVersion(const ScrollSignal& sig);

}  // namespace lists

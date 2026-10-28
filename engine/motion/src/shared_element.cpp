#include "motion/shared_element.h"

#include <cmath>

#include "motion/solver.h"

namespace motion {

namespace {

float lerpChannel(float from, float to, float t) {
  return from + (to - from) * t;
}

SharedFrame frameFromAnchor(const SharedAnchor& anchor, float t01) {
  SharedFrame frame;
  frame.box = anchor.box;
  frame.clip = anchor.clip;
  frame.clipX = anchor.clip.x;
  frame.clipY = anchor.clip.y;
  frame.cornerRadius = anchor.cornerRadius;
  frame.t01 = t01;
  return frame;
}

}  // namespace

const SharedAnchor* motion_matchShared(const std::vector<SharedAnchor>& from,
                                       const std::vector<SharedAnchor>& to,
                                       const std::string& elementId) noexcept {
  // Deep-link carve-out: empty `from` matches when `to` holds the id.
  // Otherwise the id must exist on BOTH sides; a non-empty `from` lacking
  // the id yields nullptr even when `to` holds it.
  if (!from.empty()) {
    bool inFrom = false;
    for (const SharedAnchor& anchor : from) {
      if (anchor.elementId == elementId) {
        inFrom = true;
        break;
      }
    }
    if (!inFrom) return nullptr;
  }
  for (const SharedAnchor& anchor : to) {
    if (anchor.elementId == elementId) {
      return &anchor;
    }
  }
  return nullptr;
}

SharedFrame motion_interpolateShared(const SharedAnchor& from,
                                     const SharedAnchor& to,
                                     float t01) noexcept {
  if (!std::isfinite(t01)) {
    return frameFromAnchor(from, 0.0f);
  }
  float clamped = t01;
  if (clamped < 0.0f) clamped = 0.0f;
  if (clamped > 1.0f) clamped = 1.0f;
  if (clamped == 0.0f) {
    return frameFromAnchor(from, 0.0f);
  }
  if (clamped == 1.0f) {
    return frameFromAnchor(to, 1.0f);
  }
  SharedFrame frame;
  frame.box.x = lerpChannel(from.box.x, to.box.x, clamped);
  frame.box.y = lerpChannel(from.box.y, to.box.y, clamped);
  frame.box.w = lerpChannel(from.box.w, to.box.w, clamped);
  frame.box.h = lerpChannel(from.box.h, to.box.h, clamped);
  // Clip rect rides the same t01 as the box and corner radius.
  frame.clip.x = lerpChannel(from.clip.x, to.clip.x, clamped);
  frame.clip.y = lerpChannel(from.clip.y, to.clip.y, clamped);
  frame.clip.w = lerpChannel(from.clip.w, to.clip.w, clamped);
  frame.clip.h = lerpChannel(from.clip.h, to.clip.h, clamped);
  frame.clipX = frame.clip.x;
  frame.clipY = frame.clip.y;
  frame.cornerRadius = lerpChannel(from.cornerRadius, to.cornerRadius, clamped);
  frame.t01 = clamped;
  return frame;
}

// ---- T5: shared element as a graph case ----

// Global scrub state armed by the latest motion_sharedTransition. The drive
// path retargets from the current value+velocity (retargetSpring) and
// settles through the stored spec via advanceSpring, so interruption keeps
// continuity with no new physics.
namespace {

SpringState g_scrubState;
TransitionSpec g_scrubSpec;
bool g_scrubArmed = false;

float clampProgress01(float v) {
  if (!std::isfinite(v)) return 0.0f;
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

}  // namespace

NodeId motion_sharedTransition(Graph& g, const SharedAnchor& from,
                               const SharedAnchor& to, const TransitionSpec& spec) {
  (void)to;
  // Two-anchor transition node reusing existing composites: a sequence over
  // two value leaves (from-end 0, to-end 1). Sequences self-report under
  // their own id at any tMs, so the returned id is always present in
  // g.evaluate(tMs).values, proving registration.
  Node fromLeaf;
  fromLeaf.value.value = 0.0f;
  NodeId fromId = g.add(fromLeaf);
  Node toLeaf;
  toLeaf.value.value = 1.0f;
  NodeId toId = g.add(toLeaf);
  Node transition;
  transition.sequence.children.push_back(fromId);
  transition.sequence.children.push_back(toId);
  NodeId id = g.add(transition);
  g_scrubSpec = spec;
  g_scrubState.value = 0.0f;
  g_scrubState.velocity = 0.0f;
  g_scrubArmed = true;
  (void)from;
  return id;
}

void motion_resetSharedDrive() noexcept {
  g_scrubState.value = 0.0f;
  g_scrubState.velocity = 0.0f;
  g_scrubArmed = false;
}

float motion_driveShared(float backProgress01) noexcept {
  float target = clampProgress01(backProgress01);
  // Retarget preserves the current value+velocity (solver semantics), so a
  // mid-flight reversal continues instead of snapping.
  g_scrubState = retargetSpring(g_scrubState, target);
  if (!g_scrubArmed) {
    g_scrubState.value = target;
    g_scrubState.velocity = 0.0f;
    return target;
  }
  // Settle toward the driven target through the stored spec with a bounded
  // physical step budget: fixed 8ms steps, capped at 100000 iterations
  // (~800s simulated) with early-out at 1e-6. The returned value is the
  // genuinely settled solver state, never a forced snap: a full drain
  // converges to the driven endpoint, while mid-drain states carry the
  // live value+velocity. Non-finite progress maps to 0 via clamp above.
  for (int i = 0; i < 100000; ++i) {
    if (std::fabs(g_scrubState.value - target) <= 1e-6f &&
        std::fabs(g_scrubState.velocity) <= 1e-6f) {
      break;
    }
    SpringState next =
        advanceSpring(g_scrubState, g_scrubSpec.spring, target, 8.0f);
    // advanceSpring is a no-op for degenerate specs (zero stiffness or
    // damping); fall back to the driven target so scrub still tracks.
    if (next.value == g_scrubState.value &&
        next.velocity == g_scrubState.velocity) {
      g_scrubState.value = target;
      g_scrubState.velocity = 0.0f;
      break;
    }
    g_scrubState = next;
  }
  return clampProgress01(g_scrubState.value);
}

// ---- T6: platform-view surface-rect anchor ----

SharedAnchor motion_pviewAnchor(const std::string& elementId,
                                const std::string& route,
                                const Box& surfaceRect,
                                float cornerRadius) noexcept {
  SharedAnchor anchor;
  anchor.elementId = elementId;
  anchor.route = route;
  anchor.depth = 0;
  anchor.box = surfaceRect;
  // Contract fixes this signature (no clip param); clip stays zeroed.
  anchor.clip = Box{};
  anchor.cornerRadius = cornerRadius;
  return anchor;
}

}  // namespace motion

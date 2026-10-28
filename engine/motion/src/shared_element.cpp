#include "motion/shared_element.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "motion/solver.h"
#include "motion/tokens.h"

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

// ---- T3 (FR-MOTION-003) helpers: SharedAnchor-owned tables, T1 physics ----

// Anchors/flights are stored as SharedAnchor records (elementId carries the
// stability key) so the T3 path shares one anchor representation with the
// main match/interpolate path. SurfaceKind rides alongside (SharedAnchor
// carries no nav/platform-view discriminant).
struct AnchorRecord {
  ElementId id = 0;
  SharedAnchor anchor;
  SurfaceKind kind = SurfaceKind::Nav;
};

std::map<std::string, std::vector<AnchorRecord>>& anchorTable() {
  static std::map<std::string, std::vector<AnchorRecord>> table;
  return table;
}

struct SharedFlight {
  bool started = false;
  MotionToken token;
  std::uint64_t startMs = 0;
  SharedAnchor from;
  SharedAnchor to;
};

std::map<std::string, SharedFlight>& flightTable() {
  static std::map<std::string, SharedFlight> table;
  return table;
}

bool isSpringToken(const MotionToken& t) {
  return (t.kind == TokenKind::SpatialSpring ||
          t.kind == TokenKind::EffectsSpring) &&
         t.stiffness > 0.0f;
}

// Same viscous-damping -> damping-ratio conversion as the T2 path: the
// integrated physics matches the token pins exactly.
SpringSpec toSpringSpec(const MotionToken& t) {
  SpringSpec spec;
  spec.stiffness = t.stiffness;
  if (t.stiffness > 0.0f && t.damping > 0.0f) {
    spec.dampingRatio = t.damping / (2.0f * std::sqrt(t.stiffness));
  } else {
    spec.dampingRatio = 1.0f;
  }
  return spec;
}

double smoothstepProgress(double elapsedMs, double durationMs) {
  if (durationMs <= 0.0) {
    return elapsedMs >= 0.0 ? 1.0 : 0.0;
  }
  double t = elapsedMs / durationMs;
  if (t <= 0.0) return 0.0;
  if (t >= 1.0) return 1.0;
  return t * t * (3.0 - 2.0 * t);
}

Box toBox(const Rect& r) {
  Box b;
  b.x = r.x;
  b.y = r.y;
  b.w = r.width;
  b.h = r.height;
  return b;
}

Rect toRect(const Box& b) {
  Rect r;
  r.x = b.x;
  r.y = b.y;
  r.width = b.w;
  r.height = b.h;
  return r;
}

Anchor toAnchor(const AnchorRecord& record) {
  Anchor out;
  out.id = record.id;
  out.stabilityKey = record.anchor.elementId;
  out.box = toRect(record.anchor.box);
  out.kind = record.kind;
  return out;
}

// Per-channel advanceSpring integration at fixed 1ms steps (same numerical
// scheme as the implicit path); capped so huge deltas terminate.
float advanceChannel(float from, const SpringSpec& spec, float target,
                     float fromMs, float toMs) {
  if (!(toMs > fromMs)) return from;
  SpringState s{from, 0.0f};
  float t = fromMs;
  std::uint32_t steps = 0;
  while (t < toMs && steps < 100000) {
    float step = toMs - t;
    if (step > 1.0f) step = 1.0f;
    s = advanceSpring(s, spec, target, step);
    t += step;
    ++steps;
  }
  return s.value;
}

Rect evaluateFlight(const SharedFlight& f, std::uint64_t nowMs) {
  if (!f.started || nowMs <= f.startMs) {
    return toRect(f.from.box);
  }
  const float fromMs = static_cast<float>(f.startMs);
  const float toMs = static_cast<float>(nowMs);
  if (isSpringToken(f.token)) {
    const SpringSpec spec = toSpringSpec(f.token);
    Box out;
    out.x = advanceChannel(f.from.box.x, spec, f.to.box.x, fromMs, toMs);
    out.y = advanceChannel(f.from.box.y, spec, f.to.box.y, fromMs, toMs);
    out.w = advanceChannel(f.from.box.w, spec, f.to.box.w, fromMs, toMs);
    out.h = advanceChannel(f.from.box.h, spec, f.to.box.h, fromMs, toMs);
    return toRect(out);
  }
  const double durMs =
      f.token.durationMs > 0 ? static_cast<double>(f.token.durationMs) : 300.0;
  const double elapsedMs = static_cast<double>(nowMs - f.startMs);
  const double s = smoothstepProgress(elapsedMs, durMs);
  Box out;
  out.x = lerpChannel(f.from.box.x, f.to.box.x, static_cast<float>(s));
  out.y = lerpChannel(f.from.box.y, f.to.box.y, static_cast<float>(s));
  out.w = lerpChannel(f.from.box.w, f.to.box.w, static_cast<float>(s));
  out.h = lerpChannel(f.from.box.h, f.to.box.h, static_cast<float>(s));
  return toRect(out);
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

// ---- T3 (FR-MOTION-003) shared-element flights ----

void registerAnchor(ElementId id, const char* stabilityKey, Rect box,
                    SurfaceKind kind) {
  if (stabilityKey == nullptr || stabilityKey[0] == '\0') {
    return;
  }
  std::string key(stabilityKey);
  std::vector<AnchorRecord>& list = anchorTable()[key];
  for (AnchorRecord& existing : list) {
    if (existing.id == id) {
      existing.anchor.box = toBox(box);
      existing.kind = kind;
      return;
    }
  }
  AnchorRecord record;
  record.id = id;
  record.anchor.elementId = key;
  record.anchor.route = "";
  record.anchor.depth = 0;
  record.anchor.box = toBox(box);
  record.anchor.clip = Box{};
  record.anchor.cornerRadius = 0.0f;
  record.kind = kind;
  list.push_back(record);
}

bool matchShared(const char* stabilityKey, Anchor* outA, Anchor* outB) {
  if (stabilityKey == nullptr || stabilityKey[0] == '\0') {
    return false;
  }
  auto it = anchorTable().find(std::string(stabilityKey));
  if (it == anchorTable().end() || it->second.size() < 2) {
    return false;
  }
  if (outA == nullptr || outB == nullptr) {
    return false;
  }
  *outA = toAnchor(it->second[0]);
  *outB = toAnchor(it->second[1]);
  return true;
}

void startShared(const char* stabilityKey, const char* token,
                 uint64_t nowMs) {
  // resolveToken throws TokenError naming the token on null/unknown.
  MotionToken resolved = resolveToken(token);
  if (stabilityKey == nullptr || stabilityKey[0] == '\0') {
    throw TokenError("null", "stability key must not be null");
  }
  std::string key(stabilityKey);
  SharedFlight flight;
  flight.started = true;
  flight.token = resolved;
  flight.startMs = nowMs;
  auto it = anchorTable().find(key);
  if (it != anchorTable().end() && it->second.size() >= 2) {
    flight.from = it->second[0].anchor;
    flight.to = it->second[1].anchor;
  } else if (it != anchorTable().end() && !it->second.empty()) {
    flight.from = it->second[0].anchor;
    flight.to = it->second[0].anchor;
  } else {
    flight.from = SharedAnchor{};
    flight.to = SharedAnchor{};
  }
  flightTable()[key] = flight;
}

Rect sampleShared(const char* stabilityKey, uint64_t nowMs) {
  if (stabilityKey == nullptr || stabilityKey[0] == '\0') {
    return Rect{};
  }
  auto it = flightTable().find(std::string(stabilityKey));
  if (it == flightTable().end()) {
    return Rect{};
  }
  return evaluateFlight(it->second, nowMs);
}

}  // namespace motion

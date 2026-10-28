#include "motion/implicit.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "motion/tokens.h"

namespace motion {

namespace {

// Fixed integration step for evaluating the spring between samples.
constexpr float kStepMs = 1.0f;
// Security cap: bounds the fixed-step loop so huge/Inf deltas terminate.
// Past the cap the spring is returned as-is (caller observes the capped
// value); normal animation deltas never approach this bound.
constexpr std::uint32_t kMaxAdvanceSteps = 100000;

SpringState advanceTo(const SpringState& from, const SpringSpec& spec,
                      float target, float fromMs, float toMs) noexcept {
  if (!std::isfinite(fromMs) || !std::isfinite(toMs)) return from;
  if (!(toMs > fromMs)) return from;
  SpringState s = from;
  float t = fromMs;
  std::uint32_t steps = 0;
  while (t < toMs && steps < kMaxAdvanceSteps) {
    float step = toMs - t;
    if (step > kStepMs) step = kStepMs;
    s = advanceSpring(s, spec, target, step);
    t += step;
    ++steps;
  }
  return s;
}

// ---- T2 (FR-MOTION-002) helpers: token -> T1 solver path ----

// True for spring tokens carrying positive physics constants; those drive
// the advanceSpring path, everything else (durations/easings) takes the
// smoothstep fallback.
bool isSpringToken(const MotionToken& t) {
  return (t.kind == TokenKind::SpatialSpring ||
          t.kind == TokenKind::EffectsSpring) &&
         t.stiffness > 0.0f;
}

// Converts branch-style viscous damping (c in force = -k(x-t) - c*v) to the
// T1 damping ratio (c = 2*zeta*sqrt(k)); the integrated physics is identical.
SpringSpec toSpringSpec(const MotionToken& t) {
  SpringSpec spec;
  spec.stiffness = t.stiffness;
  if (t.stiffness > 0.0f && t.damping > 0.0f) {
    spec.dampingRatio = t.damping / (2.0f * std::sqrt(t.stiffness));
  } else {
    // Unreachable via the pinned token table (all springs carry damping);
    // critically damped so a degenerate token still settles.
    spec.dampingRatio = 1.0f;
  }
  return spec;
}

// Smoothstep fallback for non-spring (duration/easing) tokens: monotone
// 0 -> 1 over durationMs, clamped outside [startMs, startMs + durationMs].
double smoothstepProgress(double elapsedMs, double durationMs) {
  if (durationMs <= 0.0) {
    return elapsedMs >= 0.0 ? 1.0 : 0.0;
  }
  double t = elapsedMs / durationMs;
  if (t <= 0.0) return 0.0;
  if (t >= 1.0) return 1.0;
  return t * t * (3.0 - 2.0 * t);
}

// Per (node, property) T2 animation record. The token is pinned at
// markImplicit time (resolveToken values); the segment re-seeds from the
// evaluated value+velocity on every retarget for continuity.
struct ImplicitEntry {
  bool hasToken = false;
  MotionToken token;
  bool hasTransition = false;
  float startValue = 0.0f;  // value (+velocity) at startMs
  float startVelocity = 0.0f;
  float target = 0.0f;
  std::uint64_t startMs = 0;
};

using ImplicitKey = std::pair<NodeId, std::string>;

std::map<ImplicitKey, ImplicitEntry>& implicitRegistry() {
  static std::map<ImplicitKey, ImplicitEntry> entries;
  return entries;
}

SpringState evaluateImplicitEntry(const ImplicitEntry& e,
                                  std::uint64_t nowMs) {
  if (!e.hasTransition || nowMs <= e.startMs) {
    return SpringState{e.startValue, e.startVelocity};
  }
  const float fromMs = static_cast<float>(e.startMs);
  const float toMs = static_cast<float>(nowMs);
  if (isSpringToken(e.token)) {
    return advanceTo(SpringState{e.startValue, e.startVelocity},
                     toSpringSpec(e.token), e.target, fromMs, toMs);
  }
  // Duration/easing fallback: smoothstep over the token duration
  // (default 300ms when the token carries no duration).
  const double durMs =
      e.token.durationMs > 0 ? static_cast<double>(e.token.durationMs) : 300.0;
  const double elapsedMs = static_cast<double>(nowMs - e.startMs);
  const double s = smoothstepProgress(elapsedMs, durMs);
  const float value =
      e.startValue + static_cast<float>(s) * (e.target - e.startValue);
  return SpringState{value, 0.0f};
}

}  // namespace

void ImplicitRegistry::markImplicit(PropertyId p, TransitionSpec s) {
  Entry& e = entries_[p.value];
  e.implicit = true;
  e.spec = s;
}

void ImplicitRegistry::set(PropertyId p, float target, float tMs) {
  if (!std::isfinite(target) || !std::isfinite(tMs)) return;
  Entry& e = entries_[p.value];
  if (!e.implicit) {
    e.hasValue = true;
    e.active = false;
    e.target = target;
    e.state.value = target;
    e.state.velocity = 0.0f;
    e.stateMs = tMs;
    e.startMs = tMs;
    return;
  }
  if (!e.hasValue) {
    e.hasValue = true;
    e.active = false;
    e.target = target;
    e.state.value = target;
    e.state.velocity = 0.0f;
    e.stateMs = tMs;
    e.startMs = tMs;
    return;
  }
  // Retarget from the current value+velocity so the handoff is continuous.
  SpringState current = evaluate(e, tMs);
  current = retargetSpring(current, target);
  e.target = target;
  e.state = current;
  e.stateMs = tMs;
  e.startMs = tMs + e.spec.delayMs + e.spec.staggerMs;
  e.active = true;
}

float ImplicitRegistry::get(PropertyId p, float tMs) const noexcept {
  auto it = entries_.find(p.value);
  if (it == entries_.end()) return 0.0f;
  const Entry& e = it->second;
  if (!e.hasValue) return 0.0f;
  if (!std::isfinite(tMs)) return e.state.value;
  if (!e.implicit || !e.active) return e.state.value;
  return evaluate(e, tMs).value;
}

SpringState ImplicitRegistry::evaluate(const Entry& e, float tMs) const
    noexcept {
  if (!e.active) return e.state;
  if (!std::isfinite(tMs)) return e.state;
  if (!(tMs > e.startMs)) {
    // Still in the delay/stagger hold: value has not started moving.
    return e.state;
  }
  return advanceTo(e.state, e.spec.spring, e.target, e.startMs, tMs);
}

float ImplicitRegistry::getOpacity(PropertyId p, float tMs) const noexcept {
  auto it = opacity_.find(p.value);
  if (it == opacity_.end() || !it->second.removed) return 1.0f;
  const OpacityFade& o = it->second;
  if (!(tMs > o.startMs)) return 1.0f;  // No snap: still fully visible.
  SpringState s = advanceTo(o.state, o.spec.spring, 0.0f, o.startMs, tMs);
  if (s.value < 0.0f) return 0.0f;
  if (s.value > 1.0f) return 1.0f;
  return s.value;
}

void ImplicitRegistry::applyLayoutFlow(std::vector<ItemMove> moves,
                                       float tMs) {
  for (const ItemMove& m : moves) {
    auto it = entries_.find(m.p.value);
    if (it == entries_.end()) continue;  // Unknown id: skip, rest apply.
    const bool entryImplicit = it->second.implicit;
    const TransitionSpec entrySpec = it->second.spec;
    // Position/size retargets through the implicit path, so reorder and
    // compact<->expanded (a position+size target swap) animate via get().
    set(m.p, m.toIndex, tMs);
    if (m.removed) {
      OpacityFade& o = opacity_[m.p.value];
      o.removed = true;
      if (entryImplicit) {
        o.spec = entrySpec;
      } else {
        o.spec.spring.stiffness = 180.0f;
        o.spec.spring.dampingRatio = 1.0f;
        o.spec.delayMs = 0.0f;
        o.spec.staggerMs = 0.0f;
      }
      o.hasSpec = true;
      o.state.value = 1.0f;
      o.state.velocity = 0.0f;
      o.startMs = tMs + o.spec.delayMs + o.spec.staggerMs;
    } else {
      // Live item (reorder/insert preview): fully opaque.
      opacity_.erase(m.p.value);
    }
  }
}

void ImplicitRegistry::applyTheme(float colorFrom, float colorTo,
                                   float elevFrom, float elevTo, float tMs) {
  (void)colorFrom;
  (void)elevFrom;
  // Theme channels are the implicit entries already holding values, ordered
  // by PropertyId so the color/elevation routing is deterministic: the
  // lowest id carries color, the highest carries elevation. Sources retarget
  // from their current values, so the toggle animates instead of snapping.
  std::vector<std::uint32_t> ids;
  for (const auto& kv : entries_) {
    if (kv.second.implicit && kv.second.hasValue) ids.push_back(kv.first);
  }
  if (ids.empty()) return;
  std::sort(ids.begin(), ids.end());
  PropertyId color;
  color.value = ids.front();
  set(color, colorTo, tMs);
  if (ids.size() > 1) {
    PropertyId elev;
    elev.value = ids.back();
    set(elev, elevTo, tMs);
  }
}

// ---- T2 (FR-MOTION-002) declarative transitions ----

void markImplicit(NodeId id, const char* property, const char* token) {
  if (property == nullptr || property[0] == '\0') {
    throw TokenError("null", "property name must not be null");
  }
  // resolveToken throws TokenError naming the token on null/unknown.
  MotionToken resolved = resolveToken(token);
  ImplicitKey key{id, std::string(property)};
  ImplicitEntry& e = implicitRegistry()[key];
  e.hasToken = true;
  e.token = resolved;
}

void onPropertyChange(NodeId id, const char* property, float toValue,
                      uint64_t nowMs) {
  if (property == nullptr || property[0] == '\0') {
    return;  // never crash on bad names; nothing to animate
  }
  ImplicitKey key{id, std::string(property)};
  ImplicitEntry& e = implicitRegistry()[key];  // rest entry when unregistered
  if (!e.hasToken && !e.hasTransition) {
    // Unregistered pair: animate from rest with a neutral effects spring so
    // a first change still interpolates 0 -> toValue.
    e.hasToken = true;
    e.token = MotionToken{TokenKind::EffectsSpring, 0, nullptr, 150.0f, 20.0f};
  }
  const float currentTarget = e.hasTransition ? e.target : 0.0f;
  if (toValue == currentTarget) {
    // No-op: repeating the target never restarts the transition. The value
    // is confirmed at its target, so settle the segment here — sampling at
    // or after nowMs returns the target instead of rewinding along the old
    // curve (the redundant set declares the animation's end state).
    if (e.hasTransition) {
      e.startValue = e.target;
      e.startVelocity = 0.0f;
      e.startMs = nowMs;
    }
    return;
  }
  // Retarget keeps the current value+velocity for continuity: evaluate at
  // nowMs, then re-seed the segment.
  SpringState cur = evaluateImplicitEntry(e, nowMs);
  e.startValue = cur.value;
  e.startVelocity = cur.velocity;
  e.target = toValue;
  e.startMs = nowMs;
  e.hasTransition = true;
}

float sample(NodeId id, const char* property, uint64_t nowMs) {
  if (property == nullptr || property[0] == '\0') {
    return 0.0f;  // throwing is also acceptable; crashing is not
  }
  ImplicitKey key{id, std::string(property)};
  auto it = implicitRegistry().find(key);
  if (it == implicitRegistry().end()) {
    return 0.0f;
  }
  return evaluateImplicitEntry(it->second, nowMs).value;
}

}  // namespace motion

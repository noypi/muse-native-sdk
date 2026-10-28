#include "motion/implicit.h"

#include <algorithm>
#include <cmath>
#include <vector>

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

}  // namespace motion

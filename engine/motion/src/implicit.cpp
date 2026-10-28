#include "motion/implicit.h"

namespace motion {

namespace {

// Fixed integration step for evaluating the spring between samples.
constexpr float kStepMs = 1.0f;

SpringState advanceTo(const SpringState& from, const SpringSpec& spec,
                      float target, float fromMs, float toMs) noexcept {
  if (!(toMs > fromMs)) return from;
  SpringState s = from;
  float t = fromMs;
  while (t < toMs) {
    float step = toMs - t;
    if (step > kStepMs) step = kStepMs;
    s = advanceSpring(s, spec, target, step);
    t += step;
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
  if (!e.implicit || !e.active) return e.state.value;
  return evaluate(e, tMs).value;
}

SpringState ImplicitRegistry::evaluate(const Entry& e, float tMs) const
    noexcept {
  if (!e.active) return e.state;
  if (!(tMs > e.startMs)) {
    // Still in the delay/stagger hold: value has not started moving.
    return e.state;
  }
  return advanceTo(e.state, e.spec.spring, e.target, e.startMs, tMs);
}

}  // namespace motion

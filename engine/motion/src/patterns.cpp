#include "motion/patterns.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <utility>

#include "motion/solver.h"
#include "motion/tokens.h"

namespace motion {

PatternError::PatternError(std::string name, std::string reason)
    : Name(std::move(name)), Reason(std::move(reason)) {
  message_ = "motion pattern error: " + Name + ": " + Reason;
}

const char* PatternError::what() const noexcept {
  return message_.c_str();
}

namespace {

// Enter/exit endpoints. Enter visibly animates 0 -> 1; exit returns to 0.
constexpr float kEnterTarget = 1.0f;
constexpr float kExitTarget = 0.0f;
constexpr float kRestValue = 0.0f;

// Per-pattern token pins. Every number driving motion below is resolved
// from these T1 token names; the names (not values) are the only literals.
struct PatternTokens {
  const char* durationToken;  // move-phase completion horizon
  const char* springToken;    // move-phase curve (stiffness + damping)
};

PatternTokens tokensFor(PatternId p) {
  switch (p) {
    case PatternId::FadeThrough:
      return {"duration-medium2", "effects-default"};
    case PatternId::SharedAxisX:
    case PatternId::SharedAxisY:
    case PatternId::SharedAxisZ:
      return {"duration-medium1", "spatial-default"};
    case PatternId::ContainerTransform:
      return {"duration-long1", "spatial-default"};
  }
  return {"duration-medium1", "spatial-default"};
}

// Stagger before motion begins (the M3 "through" gap / axis stagger), from
// a T1 duration token. Holds continuity: re-entrant calls landing inside
// the delay observe the pre-call value exactly (within 1e-3).
int delayMs() {
  static const int cached = resolveToken("duration-medium1").durationMs;
  return cached;
}

// Converts branch-style viscous damping (c in force = -k(x-t) - c*v) to
// main's damping ratio (c = 2*zeta*sqrt(k)); same scheme as T2/T3 paths.
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

// Fixed 1ms-substep integration of the main solver between sample stamps;
// capped so huge deltas terminate. Same numerical scheme as T2/T3.
SpringState advanceTo(const SpringState& from, const SpringSpec& spec,
                      float target, double fromMs, double toMs) {
  if (!(toMs > fromMs)) return from;
  SpringState s = from;
  double t = fromMs;
  std::uint32_t steps = 0;
  while (t < toMs && steps < 100000) {
    double step = toMs - t;
    if (step > 1.0) step = 1.0;
    s = advanceSpring(s, spec, target, static_cast<float>(step));
    t += step;
    ++steps;
  }
  return s;
}

struct Flight {
  bool occupied = false;
  bool active = false;
  bool hasFlight = false;  // a flight was started at least once
  float startValue = 0.0f;
  float startVelocity = 0.0f;
  float target = 0.0f;
  uint64_t startMs = 0;
  // Resolved once per entry from the pattern's token names.
  int moveMs = 0;
  SpringSpec spring{};
  // Last carried state (advanced by carry(); read by patternVelocity).
  float curValue = 0.0f;
  float curVelocity = 0.0f;
};

using Key = std::pair<PatternId, NodeId>;

std::map<Key, Flight>& registry() {
  static std::map<Key, Flight> flights;
  return flights;
}

Flight& entryFor(PatternId p, NodeId target) {
  Key key{p, target};
  auto it = registry().find(key);
  if (it != registry().end()) {
    return it->second;
  }
  Flight f;
  const PatternTokens pinned = tokensFor(p);
  f.moveMs = resolveToken(pinned.durationToken).durationMs;
  const MotionToken spring = resolveToken(pinned.springToken);
  f.spring = toSpringSpec(spring);
  f.occupied = true;
  f.curValue = kRestValue;
  f.curVelocity = 0.0f;
  f.target = kRestValue;
  auto inserted = registry().emplace(key, f);
  return inserted.first->second;
}

// Advances the flight's cached state to nowMs along its segment:
// delay hold, then the token-parameterized spring via advanceSpring.
// Past the
// delay+duration horizon a converged segment snaps to (target, 0).
// Never travels back: nowMs at or before startMs reports the segment
// start. Returns true when the previous flight completed at/before nowMs.
bool carry(Flight& f, uint64_t nowMs) {
  if (nowMs <= f.startMs) {
    f.curValue = f.startValue;
    f.curVelocity = f.startVelocity;
    return false;
  }
  const uint64_t elapsed = nowMs - f.startMs;
  const double holdMs = static_cast<double>(delayMs());
  const double t = elapsed > static_cast<uint64_t>(holdMs)
                       ? static_cast<double>(elapsed) - holdMs
                       : 0.0;
  if (t <= 0.0) {
    f.curValue = f.startValue;
    f.curVelocity = f.startVelocity;
    return false;
  }
  const SpringState s =
      advanceTo(SpringState{f.startValue, f.startVelocity}, f.spring,
                f.target, 0.0, t);
  f.curValue = s.value;
  f.curVelocity = s.velocity;
  const auto horizon = static_cast<uint64_t>(holdMs) +
                       static_cast<uint64_t>(f.moveMs > 0 ? f.moveMs : 0);
  if (elapsed >= horizon && std::fabs(s.value - f.target) <= 1e-3f &&
      std::fabs(s.velocity) <= 1e-3f) {
    f.curValue = f.target;
    f.curVelocity = 0.0f;
    return true;
  }
  return false;
}

// Starts (or continues) a flight toward goal: carries the solver to nowMs
// first so the segment re-seeds from the current value+velocity (no snap).
// When the previous flight already converged exactly at goal there is
// nothing new to drive, so the pair rests inactive.
void drive(PatternId p, NodeId target, float goal, float velocityHint,
           bool seedVelocity, uint64_t nowMs) {
  Flight& f = entryFor(p, target);
  const bool completed = carry(f, nowMs);
  // A converged previous flight re-requested at its own endpoint has
  // nothing new to drive. Fresh entries always start their flight so a
  // first runExit still reports active.
  if (f.hasFlight && completed && f.curValue == goal) {
    f.active = false;
    f.startValue = f.curValue;
    f.startVelocity = 0.0f;
    f.target = goal;
    f.startMs = nowMs;
    return;
  }
  f.startValue = f.curValue;
  f.startVelocity = seedVelocity ? velocityHint : f.curVelocity;
  f.target = goal;
  f.startMs = nowMs;
  // The re-seed point is the freshly carried state: publish it so a
  // same-stamp patternVelocity reads the continuity point (and a seeded
  // velocity hint) rather than the pre-carry cache.
  f.curValue = f.startValue;
  f.curVelocity = f.startVelocity;
  f.hasFlight = true;
  f.active = true;
}

}  // namespace

PatternId patternNamed(const char* name) {
  if (name == nullptr) {
    throw PatternError("null", "pattern name must not be null");
  }
  if (name[0] == '\0') {
    throw PatternError("empty", "unknown pattern: empty pattern name");
  }
  if (std::strcmp(name, "fade-through") == 0) {
    return PatternId::FadeThrough;
  }
  if (std::strcmp(name, "shared-axis-x") == 0) {
    return PatternId::SharedAxisX;
  }
  if (std::strcmp(name, "shared-axis-y") == 0) {
    return PatternId::SharedAxisY;
  }
  if (std::strcmp(name, "shared-axis-z") == 0) {
    return PatternId::SharedAxisZ;
  }
  if (std::strcmp(name, "container-transform") == 0) {
    return PatternId::ContainerTransform;
  }
  throw PatternError(name, std::string("unknown pattern: ") + name);
}

void runEnter(PatternId p, NodeId target, uint64_t nowMs) {
  drive(p, target, kEnterTarget, 0.0f, false, nowMs);
}

void runExit(PatternId p, NodeId target, uint64_t nowMs) {
  drive(p, target, kExitTarget, 0.0f, false, nowMs);
}

bool patternActive(PatternId p, NodeId target) {
  Key key{p, target};
  auto it = registry().find(key);
  if (it == registry().end()) {
    return false;
  }
  return it->second.active;
}

void retarget(PatternId p, NodeId target, float toValue, float velocityHint,
              uint64_t nowMs) {
  const bool seed = velocityHint != 0.0f;
  drive(p, target, toValue, velocityHint, seed, nowMs);
}

void patternVelocity(PatternId p, NodeId target, float* value,
                     float* velocity) {
  Key key{p, target};
  auto it = registry().find(key);
  float v = kRestValue;
  float vel = 0.0f;
  if (it != registry().end()) {
    v = it->second.curValue;
    vel = it->second.curVelocity;
  }
  if (value != nullptr) {
    *value = v;
  }
  if (velocity != nullptr) {
    *velocity = vel;
  }
}

}  // namespace motion

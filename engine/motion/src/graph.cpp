#include "motion/graph.h"

#include <cmath>

#include "motion/solver.h"

namespace motion {
namespace {

enum class Kind {
  Value,
  Spring,
  Easing,
  Decay,
  Keyframe,
  Sequence,
  Parallel,
};

Kind kindOf(const Node& node) {
  if (!node.sequence.children.empty()) return Kind::Sequence;
  if (!node.parallel.children.empty()) return Kind::Parallel;
  if (!node.keyframe.keys.empty()) return Kind::Keyframe;
  if (node.spring.stiffness != 0.0f || node.spring.dampingRatio != 0.0f)
    return Kind::Spring;
  if (node.easing.durationMs != 0.0f) return Kind::Easing;
  if (node.decay.velocity != 0.0f) return Kind::Decay;
  return Kind::Value;
}

bool known(const std::vector<Node>& nodes, NodeId id) {
  return id.value < nodes.size();
}

// Duration a child occupies inside a sequence / parallel parent.
float durationOf(const std::vector<Node>& nodes, NodeId id) {
  if (!known(nodes, id)) return 0.0f;
  const Node& node = nodes[id.value];
  switch (kindOf(node)) {
    case Kind::Value:
      return 0.0f;
    case Kind::Spring:
      return node.spring.durationMs > 0.0f ? node.spring.durationMs : 0.0f;
    case Kind::Easing:
      return node.easing.durationMs > 0.0f ? node.easing.durationMs : 0.0f;
    case Kind::Decay:
      return 0.0f;
    case Kind::Keyframe:
      return node.keyframe.timesMs.empty() ? 0.0f
                                           : node.keyframe.timesMs.back();
    case Kind::Sequence: {
      float total = 0.0f;
      for (NodeId child : node.sequence.children)
        total += durationOf(nodes, child);
      return total;
    }
    case Kind::Parallel: {
      float longest = 0.0f;
      for (NodeId child : node.parallel.children) {
        const float d = durationOf(nodes, child);
        if (d > longest) longest = d;
      }
      return longest;
    }
  }
  return 0.0f;
}

float clamp01(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

// Evaluates node `index` at local time t, merging leaf values into `out`.
// Returns the node's done flag. Unknown ids are ignored (done, no output).
bool evalAt(const std::vector<Node>& nodes, std::uint32_t index, float t,
            std::map<NodeId, float>& out) {
  if (index >= nodes.size()) return true;
  const NodeId self{index};
  const Node& node = nodes[index];
  if (t < 0.0f) t = 0.0f;

  switch (kindOf(node)) {
    case Kind::Value: {
      out[self] = node.value.value;
      return true;
    }
    case Kind::Spring: {
      SpringSpec spec;
      spec.stiffness = node.spring.stiffness;
      spec.dampingRatio = node.spring.dampingRatio;
      SpringState state;
      state.value = node.spring.from;
      state.velocity = 0.0f;
      float elapsed = 0.0f;
      const float step = 8.0f;
      while (elapsed < t) {
        float dt = t - elapsed;
        if (dt > step) dt = step;
        state = advanceSpring(state, spec, node.spring.to, dt);
        elapsed += dt;
      }
      out[self] = state.value;
      const bool settled =
          std::fabs(state.value - node.spring.to) <= 0.5f &&
          std::fabs(state.velocity) <= 0.5f;
      if (node.spring.durationMs > 0.0f) return settled && t >= node.spring.durationMs;
      return settled;
    }
    case Kind::Easing: {
      const float dur = node.easing.durationMs;
      if (!(dur > 0.0f)) {
        out[self] = node.easing.to;
        return true;
      }
      const float p = clamp01(t / dur);
      out[self] = node.easing.from + (node.easing.to - node.easing.from) * p;
      return t >= dur;
    }
    case Kind::Decay: {
      out[self] = node.decay.from + node.decay.velocity * (t / 1000.0f);
      return false;
    }
    case Kind::Keyframe: {
      const std::size_t n = node.keyframe.keys.size();
      if (n == 0) return true;
      const std::vector<float>& keys = node.keyframe.keys;
      const std::vector<float>& times = node.keyframe.timesMs;
      const float first = times.empty() ? 0.0f : times.front();
      const float last = times.empty() ? 0.0f : times.back();
      if (t <= first || n == 1 || times.size() != n) {
        // Degenerate timing: hold the nearest key.
        out[self] = (t >= last && n > 1) ? keys.back() : keys.front();
        return t >= last;
      }
      if (t >= last) {
        out[self] = keys.back();
        return true;
      }
      for (std::size_t i = 1; i < n; ++i) {
        if (t <= times[i]) {
          const float span = times[i] - times[i - 1];
          const float p = (span > 0.0f) ? (t - times[i - 1]) / span : 1.0f;
          out[self] = keys[i - 1] + (keys[i] - keys[i - 1]) * clamp01(p);
          return false;
        }
      }
      out[self] = keys.back();
      return true;
    }
    case Kind::Sequence: {
      // Chain durations: child i starts when children [0, i) end.
      float total = 0.0f;
      for (NodeId child : node.sequence.children)
        total += durationOf(nodes, child);
      // Find the active (first unfinished, else last known) child.
      float start = 0.0f;
      bool haveActive = false;
      NodeId active{0};
      float activeStart = 0.0f;
      NodeId lastKnown{0};
      bool haveKnown = false;
      for (NodeId child : node.sequence.children) {
        const float d = durationOf(nodes, child);
        if (known(nodes, child)) {
          lastKnown = child;
          haveKnown = true;
          if (!haveActive && (t < start + d)) {
            active = child;
            activeStart = start;
            haveActive = true;
          }
        }
        start += d;
      }
      if (!haveKnown) {
        // Only unknown children: nothing to run; done once time advances.
        return t > 0.0f;
      }
      if (!haveActive) {
        active = lastKnown;
        // activeStart currently holds the start of the last iterated child;
        // recompute it for the tail child.
        activeStart = 0.0f;
        for (NodeId child : node.sequence.children) {
          if (child == lastKnown) break;
          activeStart += durationOf(nodes, child);
        }
      }
      std::map<NodeId, float> childOut;
      evalAt(nodes, active.value, t - activeStart, childOut);
      for (const auto& kv : childOut) out[kv.first] = kv.second;
      // Expose the active leaf value under the sequence's own id.
      const auto selfIt = childOut.find(active);
      if (selfIt != childOut.end()) {
        out[self] = selfIt->second;
      } else if (!childOut.empty()) {
        out[self] = childOut.begin()->second;
      }
      if (t < total) return false;
      if (total <= 0.0f && t <= 0.0f) return false;
      // Past the end: every known child must read done at its clamped slot.
      float s = 0.0f;
      for (NodeId child : node.sequence.children) {
        const float d = durationOf(nodes, child);
        if (known(nodes, child)) {
          float local = t - s;
          if (local < 0.0f) local = 0.0f;
          if (local > d) local = d;
          std::map<NodeId, float> tmp;
          if (!evalAt(nodes, child.value, local, tmp)) return false;
        }
        s += d;
      }
      return true;
    }
    case Kind::Parallel: {
      bool allDone = true;
      bool haveKnown = false;
      for (NodeId child : node.parallel.children) {
        if (!known(nodes, child)) continue;
        haveKnown = true;
        if (!evalAt(nodes, child.value, t, out)) allDone = false;
      }
      return !haveKnown || allDone;
    }
  }
  return true;
}

}  // namespace

NodeId Graph::add(Node node) {
  const auto id = static_cast<std::uint32_t>(nodes_.size());
  nodes_.push_back(node);
  return NodeId{id};
}

Sample Graph::evaluate(float tMs) const noexcept {
  Sample sample;
  sample.done = true;
  float t = tMs;
  if (!(t >= 0.0f)) t = 0.0f;
  for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(nodes_.size());
       ++i) {
    if (!evalAt(nodes_, i, t, sample.values)) sample.done = false;
  }
  return sample;
}

}  // namespace motion

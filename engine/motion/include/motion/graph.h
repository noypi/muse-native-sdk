#ifndef MOTION_GRAPH_H_
#define MOTION_GRAPH_H_

#include <cstdint>
#include <map>
#include <vector>

namespace motion {

// Opaque handle into a Graph's node table. Default-constructed ids and ids
// never returned by Graph::add are unknown; evaluating a composite that
// references one ignores it (done, no crash, no value).
struct NodeId {
  std::uint32_t value = 0;
  bool operator==(const NodeId& other) const { return value == other.value; }
  bool operator!=(const NodeId& other) const { return value != other.value; }
  bool operator<(const NodeId& other) const { return value < other.value; }
};

struct ValueNode {
  float value = 0.0f;
};

struct SpringNode {
  float from = 0.0f;
  float to = 0.0f;
  float stiffness = 0.0f;
  float dampingRatio = 0.0f;
  float durationMs = 0.0f;
};

struct EasingNode {
  float from = 0.0f;
  float to = 0.0f;
  float durationMs = 0.0f;
};

struct DecayNode {
  float from = 0.0f;
  float velocity = 0.0f;
};

struct KeyframeNode {
  std::vector<float> keys;
  std::vector<float> timesMs;
};

struct SequenceNode {
  std::vector<NodeId> children;
};

struct ParallelNode {
  std::vector<NodeId> children;
};

// A node carries every shape; the active shape is inferred at add/evaluate
// time: non-empty sequence children win, then non-empty parallel children,
// then non-empty keyframe keys, then a configured spring (stiffness or
// dampingRatio set), then a configured easing (durationMs set), then a
// configured decay (velocity set); otherwise the node is a value leaf.
struct Node {
  ValueNode value;
  SpringNode spring;
  EasingNode easing;
  DecayNode decay;
  KeyframeNode keyframe;
  SequenceNode sequence;
  ParallelNode parallel;
};

struct Sample {
  std::map<NodeId, float> values;
  bool done = true;
};

// Pure C++ animation graph. All evaluation takes an explicit tMs (mocked
// clock); no wall clock, no JS symbols. Sequence children run one after
// another (durations chain); parallel children run together (maps merge).
// Composites nest arbitrarily in either direction.
class Graph {
 public:
  Graph() = default;

  NodeId add(Node node);
  Sample evaluate(float tMs) const noexcept;

 private:
  std::vector<Node> nodes_;
};

}  // namespace motion

#endif  // MOTION_GRAPH_H_

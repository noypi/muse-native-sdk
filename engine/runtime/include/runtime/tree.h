#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace runtime {

// Stable integer id assigned once at instantiate; never reassigned on update.
using NodeId = std::uint32_t;
inline constexpr NodeId kInvalidNodeId = static_cast<NodeId>(-1);

// Style channel (T5): minimal flexbox-subset properties per AC-RUNTIME-004.
// Carried on CompiledNode (input fixture) and Node (live tree).
enum class Direction {
  Column,
  Row,
};

enum class Justify {
  Start,
  Center,
  End,
};

enum class Align {
  Start,
  Center,
  End,
};

struct Style {
  Direction direction = Direction::Column;
  Justify justify = Justify::Start;
  Align align = Align::Start;
  float gap = 0.0f;
  float padding = 0.0f;
  float margin = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
  float flex = 0.0f;
};

// Plain C++ fixture describing one compiled-tree input node (T2 detail).
// Byte-format fidelity with the real compiler is out of scope (plan A3);
// `parent` is the parent index in the input array, or -1 for the root.
struct CompiledNode {
  int parent = -1;
  int tag = 0;
  Style style;
};

// Live node in the instantiated tree.
struct Node {
  NodeId id = kInvalidNodeId;
  int parent = -1;
  bool dirty = false;
  int tag = 0;
  Style style;
};

class RuntimeTree {
 public:
  RuntimeTree() = default;

  const Node& get(NodeId id) const;
  std::size_t size() const;
  void markDirty(NodeId id);
  std::size_t dirtyCount() const;
  bool isInstantiated(NodeId id) const;

 private:
  friend NodeId instantiate(const CompiledNode* nodes, std::size_t count,
                            RuntimeTree& out);
  std::vector<Node> nodes_;
};

// Instantiate a compiled tree into a live node tree; returns the root id.
// Duplicate/orphan input is reported via error code, never a crash:
// returns kInvalidNodeId on error (null input handled as empty).
NodeId instantiate(const CompiledNode* nodes, std::size_t count,
                  RuntimeTree& out);

}  // namespace runtime

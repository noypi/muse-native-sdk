#ifndef GRAPHICS_GRAPH_RENDER_GRAPH_H_
#define GRAPHICS_GRAPH_RENDER_GRAPH_H_

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace graphics {

// Backend-independent backend identifier for graph submission tests.
enum class BackendKind {
  VULKAN13_DYNAMIC,
  VULKAN11_EMULATION,
  GLES32,
};

// Thrown only by the dispatch layer when the backend refuses the graph.
// what() is exactly "BACKEND_REJECTED" and carries no backend internals.
class BackendRejectedError : public std::runtime_error {
 public:
  BackendRejectedError() : std::runtime_error("BACKEND_REJECTED") {}
};

// Backend-independent DAG node list. Construction never throws
// BackendRejectedError; that error originates only in submit_graph's
// dispatch layer.
class RenderGraph {
 public:
  RenderGraph() = default;

  void add_pass(const std::string& name) { passes_.push_back(name); }
  std::size_t node_count() const { return passes_.size(); }
  const std::vector<std::string>& passes() const { return passes_; }

 private:
  std::vector<std::string> passes_;
};

// Per-frame receipt returned by submit_graph.
class FrameReceipt {
 public:
  explicit FrameReceipt(bool ok) : ok_(ok) {}
  bool frame_ok() const { return ok_; }

 private:
  bool ok_ = false;
};

using DispatchFn = std::function<void(const RenderGraph&)>;

// Test-only seam replacing the backend dispatch target. The fake selects the
// mocked backend per call; the graph itself is never mutated by dispatch.
void set_dispatch_for_test(DispatchFn fn);

// Submits a backend-independent node list; returns a per-frame receipt.
// BACKEND_REJECTED surfaces only from the dispatch layer.
FrameReceipt submit_graph(const RenderGraph& graph);

}  // namespace graphics

#endif  // GRAPHICS_GRAPH_RENDER_GRAPH_H_

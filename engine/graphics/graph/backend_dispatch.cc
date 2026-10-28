#include "graph/render_graph.h"

#include <utility>

namespace graphics {
namespace {

// Default dispatch accepts any graph on the mocked backend.
DispatchFn g_dispatch = [](const RenderGraph&) {};
}  // namespace

void set_dispatch_for_test(DispatchFn fn) {
  if (fn) {
    g_dispatch = std::move(fn);
  } else {
    g_dispatch = [](const RenderGraph&) {};
  }
}

FrameReceipt submit_graph(const RenderGraph& graph) {
  // Backend-independent node list is forwarded unchanged; only this dispatch
  // layer may throw BackendRejectedError.
  g_dispatch(graph);
  return FrameReceipt(true);
}

}  // namespace graphics

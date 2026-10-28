#include "graph/render_graph.h"

#include <mutex>
#include <utility>

namespace graphics {
namespace {

// Default dispatch accepts any graph on the mocked backend. Guarded by
// g_dispatch_mu like workaround_db.cc's g_mu; the dispatch fn is copied
// under the lock and invoked outside it so user code never runs locked.
std::mutex g_dispatch_mu;
DispatchFn g_dispatch = [](const RenderGraph&) {};
}  // namespace

void set_dispatch_for_test(DispatchFn fn) {
  std::lock_guard<std::mutex> lock(g_dispatch_mu);
  if (fn) {
    g_dispatch = std::move(fn);
  } else {
    g_dispatch = [](const RenderGraph&) {};
  }
}

FrameReceipt submit_graph(const RenderGraph& graph) {
  // Backend-independent node list is forwarded unchanged; only this dispatch
  // layer may throw BackendRejectedError.
  DispatchFn fn;
  {
    std::lock_guard<std::mutex> lock(g_dispatch_mu);
    fn = g_dispatch;
  }
  fn(graph);
  return FrameReceipt(true);
}

}  // namespace graphics

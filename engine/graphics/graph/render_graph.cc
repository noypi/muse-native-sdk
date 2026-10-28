#include "graph/render_graph.h"

namespace graphics {

// Graph construction and receipt storage live here; dispatch (and therefore
// any BackendRejectedError) lives in backend_dispatch.cc so construction can
// never emit the dispatch-layer error.

}  // namespace graphics

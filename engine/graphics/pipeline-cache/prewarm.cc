#include "shaders/shader_bundle.h"

#include <algorithm>

namespace graphics {

// Warms every first-frame-critical pipeline up front. Each id is resolved
// through load_shader so unknown ids fail loudly instead of being skipped.
// Idempotent: warming the same set twice yields equal receipts. Duplicate
// input ids are deduped, keeping first-occurrence order.
PrewarmReceipt prewarm(const std::vector<ShaderId>& critical) {
  std::vector<ShaderId> warmed;
  warmed.reserve(critical.size());
  for (const ShaderId& id : critical) {
    // Throws UnknownShaderError on unknown/hostile ids — never skipped.
    (void)load_shader(id);
    if (std::find(warmed.begin(), warmed.end(), id) == warmed.end()) {
      warmed.push_back(id);
    }
  }
  return PrewarmReceipt(std::move(warmed));
}

}  // namespace graphics

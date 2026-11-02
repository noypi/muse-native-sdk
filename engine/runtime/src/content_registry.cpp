// Content-tag registry (FR-RUNTIME-013 / AC-RUNTIME-038/039): fail-closed
// tag -> handlers seam. Dispatch resolves content by tag alone; no per-app
// token exists anywhere on this path.
#include "runtime/content_registry.h"

namespace runtime {

bool ContentRegistry::Register(ContentTag tag,
                               const ContentHandlers& handlers) {
  if (tag.empty()) {
    return false;
  }
  auto [it, inserted] = entries_.emplace(std::string(tag), handlers);
  (void)it;
  return inserted;
}

const ContentHandlers* ContentRegistry::Resolve(ContentTag tag) const {
  if (tag.empty()) {
    return nullptr;
  }
  auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return nullptr;
  }
  return &it->second;
}

}  // namespace runtime

runtime::ContentRegistry& GlobalContentRegistry() {
  static runtime::ContentRegistry instance;
  return instance;
}

#include "layout/delegate.h"

namespace layout {

bool registerDelegate(Solver& s, NodeId subtreeRoot, LayoutDelegate* d) noexcept {
  try {
    if (d == nullptr) {
      return false;
    }
    if (static_cast<std::size_t>(subtreeRoot) >= s.nodes_.size()) {
      return false;  // Unknown root: reject without throwing.
    }
    for (auto& entry : s.delegates_) {
      if (entry.first == subtreeRoot) {
        entry.second = d;  // Double registration replaces.
        return true;
      }
    }
    s.delegates_.emplace_back(subtreeRoot, d);
    return true;
  } catch (...) {
    return false;  // Never throws across the module boundary.
  }
}

void unregisterDelegate(Solver& s, NodeId subtreeRoot) noexcept {
  try {
    auto& registered = s.delegates_;
    for (auto it = registered.begin(); it != registered.end(); ++it) {
      if (it->first == subtreeRoot) {
        registered.erase(it);
        break;
      }
    }
  } catch (...) {
    // Erasing never throws in practice; swallow to honor noexcept.
  }
}

}  // namespace layout

#include "muse/state/sandbox.hpp"

#include <cstring>

namespace muse {
namespace state {

Status Sandbox::check(const Descriptor& d) noexcept {
  // Audit BOTH surfaces of the unified canonical Descriptor: node names and
  // op names. Exact, case-sensitive denylist: native library loading and code
  // execution are never instantiable from a server descriptor. There is no
  // dlopen/name-to-address path anywhere in serverui, so these names are
  // denied here regardless of signature state.
  for (std::size_t i = 0; i < d.nodeCount(); ++i) {
    const char* name = d.nodeName(i);
    if (name == nullptr) continue;
    if (std::strcmp(name, "loadLibrary") == 0 ||
        std::strcmp(name, "execCode") == 0) {
      return Status::kCorrupt;
    }
  }
  for (std::size_t i = 0; i < d.opCount(); ++i) {
    const char* name = d.opName(i);
    if (name == nullptr) continue;
    if (std::strcmp(name, "loadLibrary") == 0 ||
        std::strcmp(name, "execCode") == 0) {
      return Status::kCorrupt;
    }
  }
  return Status::kOk;
}

}  // namespace state
}  // namespace muse

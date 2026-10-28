#include "caps/validation_gate.h"

namespace graphics {

// Pure, deterministic gate. DEV and PROFILE enable validation; RELEASE and
// any unrecognised value fail closed. The layer name itself is declared in
// build-config.json (dev/profile sections only); this translation unit
// never names a layer, so no release load path can originate here.
bool validation_enabled(BuildType build) {
  switch (build) {
    case BuildType::DEV:
      return true;
    case BuildType::PROFILE:
      return true;
    case BuildType::RELEASE:
      return false;
    default:
      return false;
  }
}

}  // namespace graphics

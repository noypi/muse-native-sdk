#include "runtime/vulkan_baseline.h"

namespace runtime {

BaselineCheck checkBaseline(const DeviceCaps& caps) {
  if (caps.apiMajor < 1 || (caps.apiMajor == 1 && caps.apiMinor < 1)) {
    BaselineCheck out;
    out.supported = false;
    out.error = "unsupported Vulkan version; Vulkan 1.1 baseline required";
    return out;
  }
  for (const auto& ext : caps.enabledExtensions) {
    if (ext.find("dynamic_rendering") != std::string::npos) {
      BaselineCheck out;
      out.supported = false;
      out.error = "unsupported extension '" + ext +
                  "': dynamic_rendering is above the Vulkan 1.1 baseline probed set";
      return out;
    }
  }
  BaselineCheck out;
  out.supported = true;
  out.error.clear();
  return out;
}

}  // namespace runtime

#include "device/backend_probe.h"

namespace graphics {

BackendSelection select_backend(const DeviceProbe& probe) {
  // A broken Vulkan driver disqualifies every Vulkan path, even when the
  // device advertises Vulkan 1.3 dynamic rendering.
  const bool vulkan_usable = !probe.driver_broken_vulkan;
  if (vulkan_usable && probe.vk13_dynamic_rendering) {
    return BackendSelection::VULKAN13_DYNAMIC;
  }
  if (vulkan_usable && probe.vk110_available) {
    return BackendSelection::VULKAN11_EMULATION;
  }
  if (probe.gles32_available) {
    return BackendSelection::GLES32;
  }
  throw NoViableBackendError();
}

}  // namespace graphics

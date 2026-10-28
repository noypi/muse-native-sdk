#ifndef GRAPHICS_DEVICE_BACKEND_PROBE_H_
#define GRAPHICS_DEVICE_BACKEND_PROBE_H_

#include <stdexcept>
#include <string>

namespace graphics {

// Probe result describing which backend paths the device/driver advertises.
struct DeviceProbe {
  bool vk13_dynamic_rendering = false;
  bool vk110_available = false;
  bool gles32_available = false;
  bool driver_broken_vulkan = false;
};

enum class BackendSelection {
  VULKAN13_DYNAMIC,
  VULKAN11_EMULATION,
  GLES32,
};

// Thrown when no viable backend exists. what() is exactly
// "NO_VIABLE_BACKEND" and carries no probe internals.
class NoViableBackendError : public std::runtime_error {
 public:
  NoViableBackendError() : std::runtime_error("NO_VIABLE_BACKEND") {}
};

// Selects exactly one backend path from the probe result.
BackendSelection select_backend(const DeviceProbe& probe);

}  // namespace graphics

#endif  // GRAPHICS_DEVICE_BACKEND_PROBE_H_

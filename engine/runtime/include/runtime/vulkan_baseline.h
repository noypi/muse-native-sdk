#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace runtime {

// Fake device capabilities (no Vulkan headers; hermetic probe input).
struct DeviceCaps {
  std::uint32_t apiMajor = 0;
  std::uint32_t apiMinor = 0;
  std::vector<std::string> enabledExtensions;
};

struct BaselineCheck {
  bool supported = false;
  std::string error;
};

// Vulkan 1.1 baseline probe (D1: Vulkan-only, no GLES path).
BaselineCheck checkBaseline(const DeviceCaps& caps);

}  // namespace runtime

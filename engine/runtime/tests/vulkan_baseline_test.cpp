// T5 contract tests — FR-RUNTIME-003 / AC-RUNTIME-007 (TC-RUNTIME-007).
// Vulkan 1.1 baseline capability probe with a clear error naming the
// missing baseline feature.
// Hermetic: C++17 + runtime/vulkan_baseline.h only; DeviceCaps is a plain
// struct (fake caps), no Vulkan device, no Vulkan headers, no network, no
// env/config reads, no filesystem, no live services.
// Binds ONLY to the T5 `Interface:` line; never to implementation.
#include <cstdio>
#include <string>

#include "runtime/vulkan_baseline.h"

namespace {

// Baseline device: Vulkan 1.1 with no extensions required.
static runtime::DeviceCaps baseline_device() {
  runtime::DeviceCaps caps;
  caps.apiMajor = 1;
  caps.apiMinor = 1;
  return caps;
}

}  // namespace

// Named case: Vulkan 1.1 caps with no extensions are supported.
static bool check_v11_no_extensions_supported() {
  const runtime::BaselineCheck r = runtime::checkBaseline(baseline_device());
  return r.supported && r.error.empty();
}

// Named case: Vulkan 1.0 is unsupported with an error naming Vulkan 1.1.
static bool check_v10_unsupported_names_11() {
  runtime::DeviceCaps caps;
  caps.apiMajor = 1;
  caps.apiMinor = 0;
  const runtime::BaselineCheck r = runtime::checkBaseline(caps);
  if (r.supported) return false;
  if (r.error.empty()) return false;
  return r.error.find("1.1") != std::string::npos;
}

// Named case: any demand for dynamic rendering is unsupported with a clear
// error naming dynamic rendering.
static bool check_dynamic_rendering_demand_unsupported() {
  runtime::DeviceCaps caps = baseline_device();
  caps.enabledExtensions.push_back("VK_KHR_dynamic_rendering");
  const runtime::BaselineCheck r = runtime::checkBaseline(caps);
  if (r.supported) return false;
  if (r.error.empty()) return false;
  const std::string lower = r.error;
  return lower.find("ynamic_rendering") != std::string::npos;
}

// Named case: every unsupported verdict carries a non-empty error naming
// the missing baseline feature (major version 0 probe).
static bool check_unsupported_always_carries_error() {
  runtime::DeviceCaps caps;
  caps.apiMajor = 0;
  caps.apiMinor = 9;
  const runtime::BaselineCheck r = runtime::checkBaseline(caps);
  return !r.supported && !r.error.empty();
}

// Named case: higher minor version (1.2) still satisfies the 1.1 baseline.
static bool check_v12_satisfies_baseline() {
  runtime::DeviceCaps caps;
  caps.apiMajor = 1;
  caps.apiMinor = 2;
  const runtime::BaselineCheck r = runtime::checkBaseline(caps);
  return r.supported;
}

// Named case: unrelated advertised extensions do not break the baseline.
static bool check_unrelated_extensions_ok() {
  runtime::DeviceCaps caps = baseline_device();
  caps.enabledExtensions.push_back("VK_KHR_swapchain");
  const runtime::BaselineCheck r = runtime::checkBaseline(caps);
  return r.supported;
}

#define RUN_CASE(name)                           \
  do {                                           \
    if (!(name)()) {                             \
      std::printf("FAIL %s\n", #name);           \
      ++failures;                                \
    } else {                                     \
      std::printf("PASS %s\n", #name);           \
    }                                            \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_v11_no_extensions_supported);
  RUN_CASE(check_v10_unsupported_names_11);
  RUN_CASE(check_dynamic_rendering_demand_unsupported);
  RUN_CASE(check_unsupported_always_carries_error);
  RUN_CASE(check_v12_satisfies_baseline);
  RUN_CASE(check_unrelated_extensions_ok);
  if (failures > 0) {
    std::printf("vulkan_baseline_test: %d failing\n", failures);
    return 1;
  }
  std::printf("vulkan_baseline_test: all passing\n");
  return 0;
}

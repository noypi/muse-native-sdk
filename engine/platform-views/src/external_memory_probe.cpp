// T1: zero-copy import/fallback probe (NFR-PVIEWS-001).
// Capability query for VK_ANDROID_external_memory_android_hardware_buffer
// with an API 29+ gate. Zero-copy when present, bounded-copy fallback
// otherwise. Never throws by contract.

#include "pviews/external_memory_probe.h"

ProbeResult probeExternalMemory(int apiLevel, bool extMemSupported) noexcept {
  try {
    if (apiLevel >= 29 && extMemSupported) {
      return ProbeResult{
          true, Fallback::NONE,
          "zero-copy via "
          "VK_ANDROID_external_memory_android_hardware_buffer (API " +
              std::to_string(apiLevel) + ")"};
    }
    if (apiLevel < 29) {
      return ProbeResult{
          false, Fallback::BOUNDED_COPY,
          "capability-missing: API level " + std::to_string(apiLevel) +
              " below 29, VK_ANDROID_external_memory_android_hardware_buffer "
              "unavailable"};
    }
    return ProbeResult{
        false, Fallback::BOUNDED_COPY,
        "capability-missing: "
        "VK_ANDROID_external_memory_android_hardware_buffer unavailable"};
  } catch (...) {
    return ProbeResult{false, Fallback::BOUNDED_COPY,
                       "capability-missing: probe failed"};
  }
}

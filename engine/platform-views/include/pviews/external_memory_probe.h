#pragma once

#include <string>

// T1: zero-copy import/fallback probe (NFR-PVIEWS-001).
// Queries VK_ANDROID_external_memory_android_hardware_buffer availability
// behind an API 29+ gate. Never throws.

enum class Fallback {
  NONE,
  BOUNDED_COPY,
};

struct ProbeResult {
  bool zeroCopy;
  Fallback fallback;
  std::string reason;
};

ProbeResult probeExternalMemory(int apiLevel, bool extMemSupported) noexcept;

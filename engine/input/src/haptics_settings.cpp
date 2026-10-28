#include "input/haptics_settings.h"

namespace input {
namespace {

// Process-local settings store. The platform vibrator call lives outside
// unit scope (no NDK include here): callers must consult ShouldPlayHaptic()
// first and issue no platform vibration call when it returns false.
// Suppression is not an error.
HapticsSettings g_settings{};

}  // namespace

void SetHapticsSettings(const HapticsSettings& s) noexcept { g_settings = s; }

HapticsSettings GetHapticsSettings() noexcept { return g_settings; }

bool ShouldPlayHaptic() noexcept {
  if (!g_settings.systemEnabled) return false;
  if (g_settings.tier == HapticsCapabilityTier::None) return false;
  return true;
}

}  // namespace input

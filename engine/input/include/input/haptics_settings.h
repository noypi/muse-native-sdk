#pragma once

namespace input {

enum class HapticsCapabilityTier {
  None,
  Basic,
  Waveform,
  PrimitiveComposition,
};

struct HapticsSettings {
  bool systemEnabled = true;
  HapticsCapabilityTier tier = HapticsCapabilityTier::Basic;
};

void SetHapticsSettings(const HapticsSettings& s) noexcept;
HapticsSettings GetHapticsSettings() noexcept;
bool ShouldPlayHaptic() noexcept;

}  // namespace input

// T2 contract tests — FR-INPUT-004 / AC-INPUT-006 (TC-INPUT-006).
// Hermetic: C++17 + input headers only; no network, no env/config reads,
// no filesystem, no live services, no NDK vibrator include (platform calls
// are faked in-process per plan A4 — here the gate boolean is the observable
// boundary; the sink-call suppression half is owned by the coder's null-sink
// wiring and asserted via ShouldPlayHaptic()==false meaning "no call issued").
// Binds to the T2 `Interface:` line (reproduced verbatim in T2.md):
//   `enum class HapticsCapabilityTier { None, Basic, Waveform,
//    PrimitiveComposition }; struct HapticsSettings { bool systemEnabled =
//    true; HapticsCapabilityTier tier = HapticsCapabilityTier::Basic; };
//    void SetHapticsSettings(const HapticsSettings& s);
//    HapticsSettings GetHapticsSettings(); bool ShouldPlayHaptic();`
// Include path follows the engine/runtime convention:
//   #include "input/haptics_settings.h" with engine/input/include on the path.
//
// ASSUMPTION A1: hermetic ctest suite, printf PASS/FAIL, nonzero on failure,
// same shape as engine/input/tests/event_model_test.cpp.
// ASSUMPTION A4: haptics platform calls are faked in-process (counter/sink);
// no NDK vibrator dependency in unit tests.

#include <cstdio>

#include "input/haptics_settings.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
  if (ok) {
    std::printf("PASS %s\n", name);
  } else {
    std::printf("FAIL %s\n", name);
    ++g_failures;
  }
}

void reset_default() {
  input::HapticsSettings s;
  s.systemEnabled = true;
  s.tier = input::HapticsCapabilityTier::Basic;
  input::SetHapticsSettings(s);
}

// AC-INPUT-006: enabled + capable passes the gate.
void case_enabled_basic_plays() {
  reset_default();
  check(input::ShouldPlayHaptic(), "enabled_basic_plays");
}

// AC-INPUT-006 denial: systemEnabled==false suppresses (not an error).
void case_system_disabled_suppresses() {
  reset_default();
  input::HapticsSettings s;
  s.systemEnabled = false;
  s.tier = input::HapticsCapabilityTier::Basic;
  input::SetHapticsSettings(s);
  check(!input::ShouldPlayHaptic(), "system_disabled_suppresses");
}

// AC-INPUT-006 denial: tier==None suppresses even when system-enabled.
void case_tier_none_suppresses() {
  reset_default();
  input::HapticsSettings s;
  s.systemEnabled = true;
  s.tier = input::HapticsCapabilityTier::None;
  input::SetHapticsSettings(s);
  check(!input::ShouldPlayHaptic(), "tier_none_suppresses");
}

// Both off at once still suppresses (suppressed, not an error).
void case_disabled_and_none_suppresses() {
  input::HapticsSettings s;
  s.systemEnabled = false;
  s.tier = input::HapticsCapabilityTier::None;
  input::SetHapticsSettings(s);
  check(!input::ShouldPlayHaptic(), "disabled_and_none_suppresses");
}

// Higher tiers also pass when system-enabled.
void case_waveform_and_primitive_tiers_play() {
  input::HapticsSettings s;
  s.systemEnabled = true;
  s.tier = input::HapticsCapabilityTier::Waveform;
  input::SetHapticsSettings(s);
  bool wave = input::ShouldPlayHaptic();
  s.tier = input::HapticsCapabilityTier::PrimitiveComposition;
  input::SetHapticsSettings(s);
  bool prim = input::ShouldPlayHaptic();
  check(wave && prim, "waveform_and_primitive_tiers_play");
}

// Set/Get round-trips the stored settings.
void case_set_get_round_trip() {
  input::HapticsSettings s;
  s.systemEnabled = false;
  s.tier = input::HapticsCapabilityTier::Waveform;
  input::SetHapticsSettings(s);
  input::HapticsSettings got = input::GetHapticsSettings();
  check(got.systemEnabled == false &&
            got.tier == input::HapticsCapabilityTier::Waveform,
        "set_get_round_trip");
  reset_default();
}

// Boundary: re-enabling after suppression restores play.
void case_reenable_restores_play() {
  input::HapticsSettings s;
  s.systemEnabled = false;
  s.tier = input::HapticsCapabilityTier::Basic;
  input::SetHapticsSettings(s);
  bool suppressed = !input::ShouldPlayHaptic();
  reset_default();
  check(suppressed && input::ShouldPlayHaptic(), "reenable_restores_play");
}

// Boundary: gate never throws and defaults are sane after reset.
void case_defaults_sane_never_throws() {
  bool threw = false;
  try {
    reset_default();
    input::HapticsSettings got = input::GetHapticsSettings();
    (void)input::ShouldPlayHaptic();
    check(got.systemEnabled == true &&
              got.tier == input::HapticsCapabilityTier::Basic,
          "defaults_sane");
  } catch (...) {
    threw = true;
  }
  check(!threw, "never_throws");
}

}  // namespace

int main() {
  case_enabled_basic_plays();
  case_system_disabled_suppresses();
  case_tier_none_suppresses();
  case_disabled_and_none_suppresses();
  case_waveform_and_primitive_tiers_play();
  case_set_get_round_trip();
  case_reenable_restores_play();
  case_defaults_sane_never_throws();
  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}

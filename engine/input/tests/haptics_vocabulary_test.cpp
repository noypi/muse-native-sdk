// T3 contract tests — FR-INPUT-010 / AC-INPUT-019 (TC-INPUT-019).
// Hermetic: C++17 + input headers only; no network, no env/config reads,
// no filesystem, no live services, no NDK vibrator include (platform calls
// are faked in-process per plan A4 — emissions are observed through the
// test probe below; ShouldPlayHaptic()==false means "no call issued").
// Binds to the T3 `Interface:` line (reproduced verbatim in T3.md):
//   `enum class HapticPattern { Click, Tick, Snap, Error, LongPress };
//    enum class GestureOutcome { Claim, Drop, Snap, Error, LongPress };
//    void Play(HapticPattern p); void AttachToGesture(int gestureId,
//    GestureOutcome outcome); HapticPattern PatternForOutcome(GestureOutcome o);`
//   — denial: suppressed (no sink call) when `ShouldPlayHaptic()==false`;
//   duplicate outcome for the same gestureId plays exactly once (dedup,
//   no error surfaced).
// Include paths follow the engine/runtime convention:
//   #include "input/haptics.h" (+ "input/haptics_settings.h" for the T2 gate)
//   with engine/input/include on the path.
//
// ASSUMPTION A1: hermetic ctest suite, printf PASS/FAIL, nonzero on failure,
// same shape as engine/input/tests/event_model_test.cpp.
// ASSUMPTION A4: haptics platform calls are faked in-process (counter/sink);
// no NDK vibrator dependency in unit tests.
// ASSUMPTION T3-SINK: the plan Interface line names Play/AttachToGesture but
// no emission accessor; these tests observe emissions through the explicitly
// assumed test-only hook
//   `void input::SetHapticTestSink(void (*)(input::HapticPattern));`
// (nullptr restores the default null sink). The hook name is NOT in the plan
// Interface line — the coder provides it (or reconciles a rename via the
// planner; GR-08 T1.10) without changing the specified behavior. All behavior
// assertions use only the plan Interface + the T2 gate.

#include <cstddef>
#include <cstdio>
#include <vector>

#include "input/haptics.h"
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

std::vector<input::HapticPattern> g_played;

void record_sink(input::HapticPattern p) { g_played.push_back(p); }

void reset_played() { g_played.clear(); }

void reset_gate_enabled() {
  input::HapticsSettings s;
  s.systemEnabled = true;
  s.tier = input::HapticsCapabilityTier::Basic;
  input::SetHapticsSettings(s);
}

void reset_gate_disabled() {
  input::HapticsSettings s;
  s.systemEnabled = false;
  s.tier = input::HapticsCapabilityTier::Basic;
  input::SetHapticsSettings(s);
}

void install_probe() {
  reset_played();
  input::SetHapticTestSink(&record_sink);
}

void uninstall_probe() { input::SetHapticTestSink(nullptr); }

// AC-INPUT-019 vocabulary map: one declared pattern per outcome.
void case_pattern_map_claim_click() {
  check(input::PatternForOutcome(input::GestureOutcome::Claim) ==
            input::HapticPattern::Click,
        "pattern_claim_is_click");
}

void case_pattern_map_drop_snap() {
  check(input::PatternForOutcome(input::GestureOutcome::Drop) ==
            input::HapticPattern::Snap,
        "pattern_drop_is_snap");
}

void case_pattern_map_snap_tick() {
  check(input::PatternForOutcome(input::GestureOutcome::Snap) ==
            input::HapticPattern::Tick,
        "pattern_snap_is_tick");
}

void case_pattern_map_error_error() {
  check(input::PatternForOutcome(input::GestureOutcome::Error) ==
            input::HapticPattern::Error,
        "pattern_error_is_error");
}

void case_pattern_map_longpress_longpress() {
  check(input::PatternForOutcome(input::GestureOutcome::LongPress) ==
            input::HapticPattern::LongPress,
        "pattern_longpress_is_longpress");
}

// AC-INPUT-019: enabled gate — each outcome emits its declared pattern.
void case_enabled_each_outcome_emits_once() {
  reset_gate_enabled();
  install_probe();
  const input::GestureOutcome outcomes[] = {
      input::GestureOutcome::Claim, input::GestureOutcome::Drop,
      input::GestureOutcome::Snap, input::GestureOutcome::Error,
      input::GestureOutcome::LongPress};
  bool ok = true;
  for (std::size_t i = 0; i < 5; ++i) {
    reset_played();
    input::AttachToGesture(100 + static_cast<int>(i), outcomes[i]);
    ok = ok && g_played.size() == 1 &&
         g_played[0] == input::PatternForOutcome(outcomes[i]);
  }
  check(ok, "enabled_each_outcome_emits_declared_pattern_once");
  uninstall_probe();
}

// Plan denial: duplicate outcome for the same gestureId plays exactly once.
void case_duplicate_outcome_same_gesture_deduped() {
  reset_gate_enabled();
  install_probe();
  input::AttachToGesture(7, input::GestureOutcome::Claim);
  input::AttachToGesture(7, input::GestureOutcome::Claim);
  input::AttachToGesture(7, input::GestureOutcome::Claim);
  check(g_played.size() == 1 &&
            g_played[0] == input::HapticPattern::Click,
        "duplicate_outcome_same_gesture_plays_once");
  uninstall_probe();
}

// Dedup key is (gestureId, outcome): different ids replay, different
// outcomes on the same id play.
void case_dedup_keyed_on_gesture_and_outcome() {
  reset_gate_enabled();
  install_probe();
  input::AttachToGesture(11, input::GestureOutcome::Claim);
  input::AttachToGesture(12, input::GestureOutcome::Claim);
  input::AttachToGesture(11, input::GestureOutcome::Drop);
  bool ok = g_played.size() == 3 &&
            g_played[0] == input::HapticPattern::Click &&
            g_played[1] == input::HapticPattern::Click &&
            g_played[2] == input::HapticPattern::Snap;
  check(ok, "dedup_keyed_on_gesture_and_outcome");
  uninstall_probe();
}

// AC-INPUT-019 denial: system disabled → no sink calls for any outcome.
void case_disabled_all_outcomes_suppressed() {
  reset_gate_disabled();
  install_probe();
  const input::GestureOutcome outcomes[] = {
      input::GestureOutcome::Claim, input::GestureOutcome::Drop,
      input::GestureOutcome::Snap, input::GestureOutcome::Error,
      input::GestureOutcome::LongPress};
  for (std::size_t i = 0; i < 5; ++i) {
    input::AttachToGesture(static_cast<int>(i), outcomes[i]);
  }
  input::Play(input::HapticPattern::Click);
  check(g_played.empty(), "disabled_all_outcomes_suppressed");
  uninstall_probe();
  reset_gate_enabled();
}

// Denial via capability tier: tier==None suppresses vocabulary playback.
void case_tier_none_suppresses_vocabulary() {
  input::HapticsSettings s;
  s.systemEnabled = true;
  s.tier = input::HapticsCapabilityTier::None;
  input::SetHapticsSettings(s);
  install_probe();
  input::AttachToGesture(3, input::GestureOutcome::Snap);
  input::Play(input::HapticPattern::Tick);
  check(g_played.empty(), "tier_none_suppresses_vocabulary");
  uninstall_probe();
  reset_gate_enabled();
}

// Boundary: re-enable after suppression restores exactly-once playback.
void case_reenable_restores_playback() {
  reset_gate_disabled();
  install_probe();
  input::AttachToGesture(21, input::GestureOutcome::Error);
  bool suppressed = g_played.empty();
  reset_gate_enabled();
  reset_played();
  input::AttachToGesture(21, input::GestureOutcome::Error);
  check(suppressed && g_played.size() == 1 &&
            g_played[0] == input::HapticPattern::Error,
        "reenable_restores_playback");
  uninstall_probe();
}

// Boundary: vocabulary calls never throw, even when suppressed.
void case_never_throws() {
  bool threw = false;
  try {
    reset_gate_disabled();
    install_probe();
    input::Play(input::HapticPattern::LongPress);
    input::AttachToGesture(99, input::GestureOutcome::LongPress);
    (void)input::PatternForOutcome(input::GestureOutcome::LongPress);
    uninstall_probe();
    reset_gate_enabled();
  } catch (...) {
    threw = true;
  }
  check(!threw, "never_throws");
}

}  // namespace

int main() {
  case_pattern_map_claim_click();
  case_pattern_map_drop_snap();
  case_pattern_map_snap_tick();
  case_pattern_map_error_error();
  case_pattern_map_longpress_longpress();
  case_enabled_each_outcome_emits_once();
  case_duplicate_outcome_same_gesture_deduped();
  case_dedup_keyed_on_gesture_and_outcome();
  case_disabled_all_outcomes_suppressed();
  case_tier_none_suppresses_vocabulary();
  case_reenable_restores_playback();
  case_never_throws();
  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}

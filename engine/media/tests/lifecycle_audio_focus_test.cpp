// T1 — lifecycle-aware playback controller (FR-MEDIA-003/AC-MEDIA-005, TC-MEDIA-005).
// Binds ONLY to the T1 `Interface:` line in plan.md; never to implementation.
//
// Hermetic: C++17 + engine/media/audio/playback_controller.h only; no threads,
// no timing, no platform linkage, no env/config, no filesystem, no live services.
//
// Expected state: RED — header engine/media/audio/playback_controller.h does not
// exist yet, so this file fails to compile until the coder implements it.
//
// Contract assumptions (see unit-tests/T1.md):
//   A-NS: everything in namespace `media::audio`, header-inline, never throws.
//   A-PAUSE: background / call / kLoss / kLossTransient from Playing land in
//     kPaused, except kCallStarted which lands in kSuspended (covers both
//     "Paused (playback) / Suspended (recording)" arms with one class).
//   A-LATCH: resume only by the same cause (Foreground<-background,
//     CallEnded<-call, Gain<-focus); cross-cause events never resume.
#include <cstdio>

#include "engine/media/audio/playback_controller.h"

// --- tiny harness (matches decode_metadata_test.cpp convention) ---------------
static int s_failures = 0;

#define CHECK(name, expr)                                       \
  do {                                                          \
    if (!(expr)) {                                              \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, name); \
      ++s_failures;                                             \
    } else {                                                    \
      std::printf("PASS %s\n", name);                           \
    }                                                           \
  } while (0)

using media::audio::FocusEvent;
using media::audio::LifecycleEvent;
using media::audio::PlaybackController;
using media::audio::PlaybackState;

static bool NeverThrows(void (*fn)()) {
  try {
    fn();
  } catch (...) {
    return false;
  }
  return true;
}

static PlaybackController* g_ctl = nullptr;
static void DoDuplicateBackground() {
  g_ctl->onLifecycle(LifecycleEvent::kBackground);
  g_ctl->onLifecycle(LifecycleEvent::kBackground);
}
static void DoUnknownFocus() {
  g_ctl->onFocus(static_cast<FocusEvent>(99));
  g_ctl->onLifecycle(static_cast<LifecycleEvent>(99));
}

static void check_initial_stopped() {
  PlaybackController c;
  CHECK("initial state is Stopped", c.state() == PlaybackState::kStopped);
  CHECK("initial ducked is false", !c.ducked());
}

static void check_start_stop() {
  PlaybackController c;
  c.start();
  CHECK("start Stopped->Playing", c.state() == PlaybackState::kPlaying);
  c.start();  // duplicate start ignored
  CHECK("duplicate start stays Playing", c.state() == PlaybackState::kPlaying);
  c.stop();
  CHECK("stop ->Stopped", c.state() == PlaybackState::kStopped);
  c.stop();  // duplicate stop ignored
  CHECK("duplicate stop stays Stopped", c.state() == PlaybackState::kStopped);
}

static void check_background_pause_resume() {
  PlaybackController c;
  c.start();
  c.onLifecycle(LifecycleEvent::kBackground);
  CHECK("background pauses",
        c.state() == PlaybackState::kPaused ||
            c.state() == PlaybackState::kSuspended);
  c.onLifecycle(LifecycleEvent::kForeground);
  CHECK("foreground resumes", c.state() == PlaybackState::kPlaying);
}

static void check_call_suspend_resume() {
  PlaybackController c;
  c.start();
  c.onLifecycle(LifecycleEvent::kCallStarted);
  CHECK("call suspends",
        c.state() == PlaybackState::kSuspended ||
            c.state() == PlaybackState::kPaused);
  CHECK("call does not duck", !c.ducked());
  c.onLifecycle(LifecycleEvent::kCallEnded);
  CHECK("call end resumes", c.state() == PlaybackState::kPlaying);
}

static void check_focus_loss_pause_no_autoplay() {
  PlaybackController c;
  // Focus loss from Stopped must not auto-play.
  c.onFocus(FocusEvent::kLoss);
  CHECK("loss from Stopped stays Stopped",
        c.state() == PlaybackState::kStopped);
  c.start();
  c.onFocus(FocusEvent::kLoss);
  CHECK("loss pauses",
        c.state() == PlaybackState::kPaused ||
            c.state() == PlaybackState::kSuspended);
  CHECK("loss does not duck", !c.ducked());
  c.onFocus(FocusEvent::kGain);
  CHECK("gain resumes after loss", c.state() == PlaybackState::kPlaying);
}

static void check_transient_pause_resume() {
  PlaybackController c;
  c.start();
  c.onFocus(FocusEvent::kLossTransient);
  CHECK("transient pauses",
        c.state() == PlaybackState::kPaused ||
            c.state() == PlaybackState::kSuspended);
  c.onFocus(FocusEvent::kGain);
  CHECK("gain resumes after transient", c.state() == PlaybackState::kPlaying);
}

static void check_transient_duck() {
  PlaybackController c;
  c.start();
  c.onFocus(FocusEvent::kLossTransientDuck);
  CHECK("duck keeps Playing", c.state() == PlaybackState::kPlaying);
  CHECK("duck flag set", c.ducked());
  c.onFocus(FocusEvent::kGain);
  CHECK("gain clears duck", !c.ducked());
  CHECK("gain after duck stays Playing",
        c.state() == PlaybackState::kPlaying);
}

static void check_cause_latch() {
  // Background-paused must NOT resume on Gain or CallEnded.
  {
    PlaybackController c;
    c.start();
    c.onLifecycle(LifecycleEvent::kBackground);
    c.onFocus(FocusEvent::kGain);
    CHECK("gain does not clear background pause",
          c.state() != PlaybackState::kPlaying);
    c.onLifecycle(LifecycleEvent::kCallEnded);
    CHECK("call-ended does not clear background pause",
          c.state() != PlaybackState::kPlaying);
    c.onLifecycle(LifecycleEvent::kForeground);
    CHECK("foreground clears background pause",
          c.state() == PlaybackState::kPlaying);
  }
  // Call-suspended must NOT resume on Foreground or Gain.
  {
    PlaybackController c;
    c.start();
    c.onLifecycle(LifecycleEvent::kCallStarted);
    c.onLifecycle(LifecycleEvent::kForeground);
    CHECK("foreground does not clear call suspend",
          c.state() != PlaybackState::kPlaying);
    c.onFocus(FocusEvent::kGain);
    CHECK("gain does not clear call suspend",
          c.state() != PlaybackState::kPlaying);
    c.onLifecycle(LifecycleEvent::kCallEnded);
    CHECK("call-ended clears call suspend",
          c.state() == PlaybackState::kPlaying);
  }
  // Focus-paused must NOT resume on Foreground.
  {
    PlaybackController c;
    c.start();
    c.onFocus(FocusEvent::kLoss);
    c.onLifecycle(LifecycleEvent::kForeground);
    CHECK("foreground does not clear focus pause",
          c.state() != PlaybackState::kPlaying);
    c.onFocus(FocusEvent::kGain);
    CHECK("gain clears focus pause", c.state() == PlaybackState::kPlaying);
  }
}

static void check_duplicate_unknown_never_throw() {
  PlaybackController c;
  g_ctl = &c;
  c.start();
  CHECK("duplicate events never throw", NeverThrows(&DoDuplicateBackground));
  CHECK("unknown events never throw", NeverThrows(&DoUnknownFocus));
  g_ctl = nullptr;
  // State stays well-defined after unknown events.
  CHECK("unknown events leave non-crash state",
        c.state() == PlaybackState::kPlaying ||
            c.state() == PlaybackState::kPaused ||
            c.state() == PlaybackState::kSuspended);
  // Unknown events from Stopped never throw either.
  PlaybackController s;
  g_ctl = &s;
  CHECK("unknown events from Stopped never throw",
        NeverThrows(&DoUnknownFocus));
  CHECK("unknown events from Stopped stay Stopped",
        s.state() == PlaybackState::kStopped);
  g_ctl = nullptr;
}

int main() {
  check_initial_stopped();
  check_start_stop();
  check_background_pause_resume();
  check_call_suspend_resume();
  check_focus_loss_pause_no_autoplay();
  check_transient_pause_resume();
  check_transient_duck();
  check_cause_latch();
  check_duplicate_unknown_never_throw();
  if (s_failures == 0) {
    std::printf("ALL GREEN lifecycle_audio_focus_test\n");
    return 0;
  }
  std::printf("%d FAILURES lifecycle_audio_focus_test\n", s_failures);
  return 1;
}

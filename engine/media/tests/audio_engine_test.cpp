// T2 — low-latency audio engine decision layer (FR-MEDIA-009/AC-MEDIA-014, TC-MEDIA-014).
// Binds ONLY to the T2 `Interface:` line in plan.md; never to implementation.
//
// Hermetic: C++17 + engine/media/audio/audio_engine.h only; no AAudio/Oboe
// linkage, no threads, no timing, no platform, no env/config, no filesystem.
//
// Expected state: RED — header engine/media/audio/audio_engine.h does not exist
// yet, so this file fails to compile until the coder implements it.
//
// Contract assumptions (see unit-tests/T2.md):
//   A-NS: everything in namespace `media::audio`, header-inline, never throws.
//   A-FMT: `enum class AudioFormat { kPcm, kWav, kAac, kMp3, kOpus }` and
//     `enum class AudioUsage { kMedia, kVoice, kAlarm, kGame }` (all accepted
//     by configure without throw; exact enumerator set pinned by this test).
//   A-FOCUS: onFocus duck/pause mirrors T1 (kLossTransientDuck ducks + stays
//     open; kLoss/kLossTransient pause; kGain resumes/clears).
//   A-ROUTE: onRoute hot-swaps atomically; stream stays open on every pair.
#include <cstdio>

#include "engine/media/audio/audio_engine.h"

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

using media::audio::AudioEngine;
using media::audio::AudioFormat;
using media::audio::AudioUsage;
using media::audio::Backend;
using media::audio::FocusEvent;
using media::audio::LatencyClass;
using media::audio::Route;
using media::audio::StreamConfig;

static void check_backend_select() {
  AudioEngine e;
  CHECK("AAudio iff both flags",
        e.selectBackend(true, true) == Backend::kAAudio);
  CHECK("no aaudio -> fallback",
        e.selectBackend(false, true) == Backend::kOboeFallback);
  CHECK("no low-latency path -> fallback",
        e.selectBackend(true, false) == Backend::kOboeFallback);
  CHECK("neither -> fallback",
        e.selectBackend(false, false) == Backend::kOboeFallback);
}

static void check_latency_class() {
  AudioEngine e;
  CHECK("low path -> Low", e.latencyClass(true) == LatencyClass::kLow);
  CHECK("normal path -> Normal",
        e.latencyClass(false) == LatencyClass::kNormal);
}

static void check_configure_all_formats() {
  bool threw = false;
  try {
    const AudioFormat kFormats[] = {AudioFormat::kPcm, AudioFormat::kWav,
                                    AudioFormat::kAac, AudioFormat::kMp3,
                                    AudioFormat::kOpus};
    for (AudioFormat f : kFormats) {
      StreamConfig cfg{};
      cfg.format = f;
      cfg.usage = AudioUsage::kMedia;
      cfg.backgroundFgs = false;
      cfg.effects = false;
      AudioEngine e;
      e.configure(cfg);
    }
    // Flags are consumed without throw.
    StreamConfig cfg{};
    cfg.format = AudioFormat::kPcm;
    cfg.usage = AudioUsage::kVoice;
    cfg.backgroundFgs = true;
    cfg.effects = true;
    AudioEngine e;
    e.configure(cfg);
  } catch (...) {
    threw = true;
  }
  CHECK("all formats + flags configure without throw", !threw);
}

static void check_configure_invalid_never_throws() {
  bool threw = false;
  try {
    StreamConfig cfg{};
    cfg.format = static_cast<AudioFormat>(99);
    cfg.usage = static_cast<AudioUsage>(99);
    AudioEngine e;
    e.configure(cfg);
    (void)e.backend();
    (void)e.route();
    (void)e.streamOpen();
  } catch (...) {
    threw = true;
  }
  CHECK("invalid config degrades without throw", !threw);
}

static void check_focus_duck_pause() {
  // Duck keeps stream open.
  {
    AudioEngine e;
    e.onFocus(FocusEvent::kLossTransientDuck);
    bool threw = false;
    try {
      e.onFocus(FocusEvent::kGain);
    } catch (...) {
      threw = true;
    }
    CHECK("duck/gain never throws", !threw);
  }
  // Loss / transient never throw, gain never throws.
  {
    AudioEngine e;
    bool threw = false;
    try {
      e.onFocus(FocusEvent::kLoss);
      e.onFocus(FocusEvent::kGain);
      e.onFocus(FocusEvent::kLossTransient);
      e.onFocus(FocusEvent::kGain);
      e.onFocus(static_cast<FocusEvent>(99));
    } catch (...) {
      threw = true;
    }
    CHECK("loss/transient/unknown never throw", !threw);
  }
}

static void check_route_matrix() {
  const Route kRoutes[] = {Route::kSpeaker, Route::kHeadset, Route::kBluetooth,
                           Route::kUsb};
  for (Route from : kRoutes) {
    for (Route to : kRoutes) {
      AudioEngine e;
      bool threw = false;
      try {
        e.onRoute(from);
        e.onRoute(to);
      } catch (...) {
        threw = true;
      }
      char nt[96], ot[96], rt[96];
      std::snprintf(nt, sizeof(nt), "route %d->%d never throws",
                    static_cast<int>(from), static_cast<int>(to));
      std::snprintf(ot, sizeof(ot), "route %d->%d stays open",
                    static_cast<int>(from), static_cast<int>(to));
      std::snprintf(rt, sizeof(rt), "route %d->%d lands on next",
                    static_cast<int>(from), static_cast<int>(to));
      CHECK(nt, !threw);
      CHECK(ot, e.streamOpen());
      CHECK(rt, e.route() == to);
    }
  }
}

static void check_invalid_route_never_throws() {
  AudioEngine e;
  bool threw = false;
  Route before = e.route();
  try {
    e.onRoute(static_cast<Route>(99));
  } catch (...) {
    threw = true;
  }
  CHECK("invalid route never throws", !threw);
  CHECK("invalid route keeps stream open", e.streamOpen());
  (void)before;
}

static void check_configure_latches_backend() {
  {
    StreamConfig cfg{};
    cfg.format = AudioFormat::kPcm;
    cfg.usage = AudioUsage::kMedia;
    cfg.aaudioAvailable = true;
    cfg.lowLatencyPath = true;
    AudioEngine e;
    e.configure(cfg);
    CHECK("configure both true -> kAAudio", e.backend() == Backend::kAAudio);
  }
  {
    StreamConfig cfg{};
    cfg.aaudioAvailable = false;
    cfg.lowLatencyPath = true;
    AudioEngine e;
    e.configure(cfg);
    CHECK("configure no aaudio -> kOboeFallback",
          e.backend() == Backend::kOboeFallback);
  }
  {
    StreamConfig cfg{};
    cfg.aaudioAvailable = true;
    cfg.lowLatencyPath = false;
    AudioEngine e;
    e.configure(cfg);
    CHECK("configure no low-latency -> kOboeFallback",
          e.backend() == Backend::kOboeFallback);
  }
  {
    StreamConfig cfg{};
    cfg.aaudioAvailable = false;
    cfg.lowLatencyPath = false;
    AudioEngine e;
    e.configure(cfg);
    CHECK("configure neither -> kOboeFallback",
          e.backend() == Backend::kOboeFallback);
  }
  {
    StreamConfig cfg{};
    AudioEngine e;
    e.configure(cfg);
    CHECK("default StreamConfig -> kOboeFallback",
          e.backend() == Backend::kOboeFallback);
  }
}

int main() {
  check_backend_select();
  check_latency_class();
  check_configure_all_formats();
  check_configure_invalid_never_throws();
  check_focus_duck_pause();
  check_route_matrix();
  check_invalid_route_never_throws();
  check_configure_latches_backend();
  if (s_failures == 0) {
    std::printf("ALL GREEN audio_engine_test\n");
    return 0;
  }
  std::printf("%d FAILURES audio_engine_test\n", s_failures);
  return 1;
}

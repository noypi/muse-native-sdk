#pragma once

#include "engine/media/audio/playback_controller.h"

namespace media {
namespace audio {

// T2 — low-latency audio engine decision layer (FR-MEDIA-009/AC-MEDIA-014).
// Header-inline, no AAudio/Oboe linkage (booleans injected), never throws.

enum class Backend {
  kAAudio,
  kOboeFallback,
};

enum class LatencyClass {
  kLow,
  kNormal,
};

enum class Route {
  kSpeaker,
  kHeadset,
  kBluetooth,
  kUsb,
};

enum class AudioFormat {
  kPcm,
  kWav,
  kAac,
  kMp3,
  kOpus,
};

enum class AudioUsage {
  kMedia,
  kVoice,
  kAlarm,
  kGame,
};

struct StreamConfig {
  AudioFormat format = AudioFormat::kPcm;
  AudioUsage usage = AudioUsage::kMedia;
  bool backgroundFgs = false;
  bool effects = false;
};

class AudioEngine {
 public:
  AudioEngine() noexcept = default;

  void configure(const StreamConfig& cfg) noexcept {
    format_ = IsValidFormat(cfg.format) ? cfg.format : AudioFormat::kPcm;
    usage_ = IsValidUsage(cfg.usage) ? cfg.usage : AudioUsage::kMedia;
    background_fgs_ = cfg.backgroundFgs;
    effects_ = cfg.effects;
    stream_open_ = true;
  }

  Backend selectBackend(bool aaudioAvailable,
                        bool lowLatencyPath) const noexcept {
    return (aaudioAvailable && lowLatencyPath) ? Backend::kAAudio
                                               : Backend::kOboeFallback;
  }

  LatencyClass latencyClass(bool lowLatencyPath) const noexcept {
    return lowLatencyPath ? LatencyClass::kLow : LatencyClass::kNormal;
  }

  void onFocus(FocusEvent ev) noexcept {
    switch (ev) {
      case FocusEvent::kLoss:
      case FocusEvent::kLossTransient:
        paused_ = true;
        ducked_ = false;
        break;
      case FocusEvent::kLossTransientDuck:
        ducked_ = true;
        break;
      case FocusEvent::kGain:
        paused_ = false;
        ducked_ = false;
        break;
      default:
        break;  // Unknown values ignored, never throws.
    }
    // Stream stays open across all focus transitions (duck or pause).
    stream_open_ = true;
  }

  void onRoute(Route next) noexcept {
    switch (next) {
      case Route::kSpeaker:
      case Route::kHeadset:
      case Route::kBluetooth:
      case Route::kUsb:
        route_ = next;
        break;
      default:
        break;  // Invalid route: keep current, never throws.
    }
    // Hot-swap: stream kept open on every pair.
    stream_open_ = true;
  }

  Backend backend() const noexcept { return backend_; }
  Route route() const noexcept { return route_; }
  bool streamOpen() const noexcept { return stream_open_; }

 private:
  static bool IsValidFormat(AudioFormat f) noexcept {
    switch (f) {
      case AudioFormat::kPcm:
      case AudioFormat::kWav:
      case AudioFormat::kAac:
      case AudioFormat::kMp3:
      case AudioFormat::kOpus:
        return true;
      default:
        return false;
    }
  }

  static bool IsValidUsage(AudioUsage u) noexcept {
    switch (u) {
      case AudioUsage::kMedia:
      case AudioUsage::kVoice:
      case AudioUsage::kAlarm:
      case AudioUsage::kGame:
        return true;
      default:
        return false;
    }
  }

  Backend backend_ = Backend::kOboeFallback;
  Route route_ = Route::kSpeaker;
  bool stream_open_ = true;
  AudioFormat format_ = AudioFormat::kPcm;
  AudioUsage usage_ = AudioUsage::kMedia;
  bool background_fgs_ = false;
  bool effects_ = false;
  bool paused_ = false;
  bool ducked_ = false;
};

}  // namespace audio
}  // namespace media

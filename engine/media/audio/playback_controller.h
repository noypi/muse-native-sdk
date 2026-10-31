#pragma once

namespace media {
namespace audio {

// T1 — lifecycle-aware playback controller (FR-MEDIA-003/AC-MEDIA-005).
// Header-inline per the image_metadata.h precedent: no threads, no OS
// linkage, never throws, all inputs total (unknown/duplicate events ignored).

enum class LifecycleEvent {
  kForeground,
  kBackground,
  kCallStarted,
  kCallEnded,
};

enum class FocusEvent {
  kGain,
  kLoss,
  kLossTransient,
  kLossTransientDuck,
};

enum class PlaybackState {
  kStopped,
  kPlaying,
  kPaused,
  kSuspended,
};

class PlaybackController {
 public:
  PlaybackController() noexcept = default;

  void start() noexcept {
    if (state_ == PlaybackState::kStopped) {
      state_ = PlaybackState::kPlaying;
      cause_ = Cause::kNone;
      ducked_ = false;
    } else if (state_ == PlaybackState::kPaused ||
               state_ == PlaybackState::kSuspended) {
      state_ = PlaybackState::kPlaying;
      cause_ = Cause::kNone;
      ducked_ = false;
    }
    // Already Playing: ignored (stays Playing, duck latch preserved).
  }

  void stop() noexcept {
    state_ = PlaybackState::kStopped;
    cause_ = Cause::kNone;
    ducked_ = false;
  }

  void onLifecycle(LifecycleEvent ev) noexcept {
    switch (ev) {
      case LifecycleEvent::kBackground:
        if (state_ == PlaybackState::kPlaying) {
          state_ = PlaybackState::kPaused;
          cause_ = Cause::kBackground;
          ducked_ = false;
        }
        break;
      case LifecycleEvent::kCallStarted:
        if (state_ == PlaybackState::kPlaying) {
          state_ = PlaybackState::kSuspended;
          cause_ = Cause::kCall;
          ducked_ = false;
        }
        break;
      case LifecycleEvent::kForeground:
        if ((state_ == PlaybackState::kPaused ||
             state_ == PlaybackState::kSuspended) &&
            cause_ == Cause::kBackground) {
          state_ = PlaybackState::kPlaying;
          cause_ = Cause::kNone;
        }
        break;
      case LifecycleEvent::kCallEnded:
        if ((state_ == PlaybackState::kPaused ||
             state_ == PlaybackState::kSuspended) &&
            cause_ == Cause::kCall) {
          state_ = PlaybackState::kPlaying;
          cause_ = Cause::kNone;
        }
        break;
      default:
        break;  // Unknown values ignored, never throws.
    }
  }

  void onFocus(FocusEvent ev) noexcept {
    switch (ev) {
      case FocusEvent::kLoss:
      case FocusEvent::kLossTransient:
        if (state_ == PlaybackState::kPlaying) {
          state_ = PlaybackState::kPaused;
          cause_ = Cause::kFocus;
          ducked_ = false;
        }
        break;
      case FocusEvent::kLossTransientDuck:
        if (state_ == PlaybackState::kPlaying) {
          ducked_ = true;
        }
        break;
      case FocusEvent::kGain:
        if (ducked_ && state_ == PlaybackState::kPlaying) {
          ducked_ = false;
        }
        if ((state_ == PlaybackState::kPaused ||
             state_ == PlaybackState::kSuspended) &&
            cause_ == Cause::kFocus) {
          state_ = PlaybackState::kPlaying;
          cause_ = Cause::kNone;
          ducked_ = false;
        }
        break;
      default:
        break;  // Unknown values ignored, never throws.
    }
  }

  PlaybackState state() const noexcept { return state_; }
  bool ducked() const noexcept { return ducked_; }

 private:
  enum class Cause { kNone, kBackground, kCall, kFocus };

  PlaybackState state_ = PlaybackState::kStopped;
  Cause cause_ = Cause::kNone;
  bool ducked_ = false;
};

}  // namespace audio
}  // namespace media

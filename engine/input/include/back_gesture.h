#pragma once

#include <cstdint>

namespace input {

enum class BackPhase { Start, Progress, Cancel, Commit };

class INavBackSink {
 public:
  virtual ~INavBackSink() = default;
  virtual void OnBackProgress(float progress) = 0;
  virtual void OnBackSettled(bool committed) = 0;
};

class BackGestureStream {
  // Sequences are Start-delimited; duplicate terminal without Start is ignored.
 public:
  explicit BackGestureStream(INavBackSink* sink) : sink_(sink) {}
  void OnEvent(BackPhase phase, float progress);
  // frameId is RESERVED for future VSYNC wiring and intentionally unused at
  // unit level.
  bool PollSettled(int32_t frameId, float* outResidual);

 private:
  INavBackSink* sink_;
  bool settled_ = false;
};

}  // namespace input

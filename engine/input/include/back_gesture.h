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
 public:
  explicit BackGestureStream(INavBackSink* sink) : sink_(sink) {}
  void OnEvent(BackPhase phase, float progress);
  bool PollSettled(int32_t frameId, float* outResidual);

 private:
  INavBackSink* sink_;
  bool settled_ = false;
};

}  // namespace input

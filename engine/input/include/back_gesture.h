#pragma once

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

 private:
  INavBackSink* sink_;
};

}  // namespace input

#include "back_gesture.h"

namespace input {

void BackGestureStream::OnEvent(BackPhase phase, float progress) {
  if (phase == BackPhase::Start) {
    settled_ = false;
  } else if (phase == BackPhase::Cancel || phase == BackPhase::Commit) {
    if (settled_) return;
    settled_ = true;
  }
  if (sink_ == nullptr) return;
  sink_->OnBackProgress(progress);
  switch (phase) {
    case BackPhase::Cancel:
      sink_->OnBackSettled(false);
      break;
    case BackPhase::Commit:
      sink_->OnBackSettled(true);
      break;
    case BackPhase::Start:
    case BackPhase::Progress:
      break;
  }
}

bool BackGestureStream::PollSettled(int32_t frameId, float* outResidual) {
  (void)frameId;
  if (!settled_) return false;
  if (outResidual != nullptr) *outResidual = 0.0f;
  return true;
}

}  // namespace input

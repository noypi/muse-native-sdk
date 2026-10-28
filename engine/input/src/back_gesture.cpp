#include "back_gesture.h"

namespace input {

void BackGestureStream::OnEvent(BackPhase phase, float progress) {
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

}  // namespace input

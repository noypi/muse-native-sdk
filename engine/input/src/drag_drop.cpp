#include "input/drag_drop.h"

namespace input {
namespace {

void (*g_observer)(DragTargetEvent, int) = nullptr;

void Notify(DragTargetEvent event, int targetId) noexcept {
  void (*observer)(DragTargetEvent, int) = g_observer;
  if (observer == nullptr) return;
  try {
    observer(event, targetId);
  } catch (...) {
    // Observer is test-only; never let it break the session state machine.
  }
}

bool IsSupportedMime(const std::string& mime) noexcept {
  try {
    if (mime.empty()) return false;
    // Supported set for unit scope: text/* payloads. Rejects
    // attacker-shaped or platform-unsupported types such as
    // "application/x-unsupported-blob".
    constexpr char kPrefix[] = "text/";
    if (mime.size() < sizeof(kPrefix) - 1) return false;
    return mime.compare(0, sizeof(kPrefix) - 1, kPrefix) == 0;
  } catch (...) {
    return false;
  }
}

}  // namespace

void SetDragTargetObserverForTesting(
    void (*observer)(DragTargetEvent event, int targetId)) noexcept {
  g_observer = observer;
}

ClipData EncodeClipData(const DragPayload& p) {
  ClipData c;
  c.mimeType = p.mimeType;
  c.bytes = p.data;
  return c;
}

DragPayload DecodeClipData(const ClipData& c) {
  DragPayload p;
  p.mimeType = c.mimeType;
  p.data = c.bytes;
  return p;
}

bool DragSession::Start(const DragPayload& p) noexcept {
  try {
    if (!IsSupportedMime(p.mimeType)) return false;
    payload_ = p;
    active_ = true;
    inside_ = false;
    return true;
  } catch (...) {
    return false;
  }
}

void DragSession::OnEnter(int targetId) noexcept {
  if (!active_) return;
  inside_ = true;
  Notify(DragTargetEvent::Enter, targetId);
}

void DragSession::OnHover(int targetId, float /*x*/, float /*y*/) noexcept {
  if (!active_ || !inside_) return;
  Notify(DragTargetEvent::Hover, targetId);
}

void DragSession::OnLeave(int targetId) noexcept {
  if (!active_) return;
  inside_ = false;
  Notify(DragTargetEvent::Leave, targetId);
}

bool DragSession::Drop(int targetId) noexcept {
  if (!active_ || !inside_) return false;
  Notify(DragTargetEvent::Drop, targetId);
  active_ = false;
  inside_ = false;
  return true;
}

void DragSession::Cancel() noexcept {
  active_ = false;
  inside_ = false;
}

}  // namespace input

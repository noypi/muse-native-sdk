#pragma once

#include <cstdint>
#include <string>
#include <vector>

// FR-INPUT-009 drag & drop session (AC-INPUT-017).
//
// Pure in-process model of the Android ClipData bridge plus the ordered
// target callbacks (enter → hover* → (drop | cancel) with leave).
// EncodeClipData / DecodeClipData copy MIME + bytes losslessly; the live
// View.OnDragListener / JNI boundary is outside unit scope (plan A5).
//
// LIST-ITEMANIM handoff: Drop notifies the settle path only via the
// DragTargetEvent::Drop observer notification. Reorder, dismiss,
// and auto-scroll are owned by specs-lists-scrolling.md and are NOT
// implemented here.

namespace input {

struct DragPayload {
  std::string mimeType;
  std::vector<std::uint8_t> data;
};

struct ClipData {
  std::string mimeType;
  std::vector<std::uint8_t> bytes;
};

ClipData EncodeClipData(const DragPayload& p);
DragPayload DecodeClipData(const ClipData& c);

enum class DragTargetEvent {
  Enter,
  Hover,
  Leave,
  Drop,
};

// Test-only target observer (ASSUMPTION T4-OBSERVER). nullptr clears.
void SetDragTargetObserverForTesting(
    void (*observer)(DragTargetEvent event, int targetId)) noexcept;

class DragSession {
 public:
  DragSession() noexcept = default;
  bool Start(const DragPayload& p) noexcept;
  void OnEnter(int targetId) noexcept;
  void OnHover(int targetId, float x, float y) noexcept;
  void OnLeave(int targetId) noexcept;
  bool Drop(int targetId) noexcept;
  void Cancel() noexcept;

 private:
  bool active_ = false;
  bool inside_ = false;
  DragPayload payload_;
};

}  // namespace input

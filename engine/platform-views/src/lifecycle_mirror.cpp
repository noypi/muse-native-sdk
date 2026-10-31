#include "pviews/lifecycle_mirror.h"

#include <cstddef>

namespace {

// Host-side mirror ledger: each (view, lifecycle, focus) step merges one
// semantics node exactly once (semantics.cpp merge pattern: duplicate scan
// first, nothing published on denial). DESTROY is terminal.
struct MirrorRecord {
  std::uint64_t view = 0;
  LifecycleEvent event = LifecycleEvent::CREATE;
  FocusEvent focus = FocusEvent::NONE;
  bool occupied = false;
};

constexpr std::size_t kMaxRecords = 256;
MirrorRecord g_records[kMaxRecords];
std::size_t g_count = 0;

bool hasRecord(std::uint64_t view, LifecycleEvent event,
               FocusEvent focus) noexcept {
  for (std::size_t i = 0; i < g_count; ++i) {
    if (g_records[i].occupied && g_records[i].view == view &&
        g_records[i].event == event && g_records[i].focus == focus) {
      return true;
    }
  }
  return false;
}

bool hasTerminal(std::uint64_t view) noexcept {
  for (std::size_t i = 0; i < g_count; ++i) {
    if (g_records[i].occupied && g_records[i].view == view &&
        g_records[i].event == LifecycleEvent::DESTROY) {
      return true;
    }
  }
  return false;
}

// Single-thread scope: g_records/g_count assume single-threaded host use;
// concurrent callers would race the ledger (see Residual Risk). No locks.
// Returns false when the 256-record ledger is full (record dropped).
bool addRecord(std::uint64_t view, LifecycleEvent event,
               FocusEvent focus) noexcept {
  if (g_count >= kMaxRecords) return false;
  g_records[g_count].view = view;
  g_records[g_count].event = event;
  g_records[g_count].focus = focus;
  g_records[g_count].occupied = true;
  ++g_count;
  return true;
}

}  // namespace

MirrorResult mirrorLifecycleFocusA11y(ViewHandle view, LifecycleEvent event,
                                      FocusEvent focus) noexcept {
  MirrorResult denied;
  denied.ok = false;

  // Invalid/null view denial (embed.h: 0 == invalid).
  if (view.value == 0) {
    denied.error = MirrorErrorCode::BAD_VIEW;
    return denied;
  }

  // Unreachable focus: gain cannot route into a destroyed view.
  if (event == LifecycleEvent::DESTROY && focus == FocusEvent::GAIN) {
    denied.error = MirrorErrorCode::FOCUS_NOT_REACHABLE;
    return denied;
  }

  // Exactly-once terminal dispatch: DESTROY already mirrored for this view.
  if (event == LifecycleEvent::DESTROY && hasTerminal(view.value)) {
    denied.error = MirrorErrorCode::SEMANTICS_MIRROR_FAILED;
    return denied;
  }
  // Any step after a terminal DESTROY is denied (no re-dispatch).
  if (event != LifecycleEvent::DESTROY && hasTerminal(view.value)) {
    denied.error = MirrorErrorCode::SEMANTICS_MIRROR_FAILED;
    return denied;
  }

  // Duplicate TalkBack node: same lifecycle step already merged exactly
  // once — deny, never merge twice (semantics.cpp duplicate pattern).
  if (hasRecord(view.value, event, focus)) {
    denied.error = MirrorErrorCode::SEMANTICS_MIRROR_FAILED;
    return denied;
  }

  // Ledger saturation is a denial, not ok=true: post-saturation duplicate
  // detection is lost, so no further step may report success.
  if (!addRecord(view.value, event, focus)) {
    denied.error = MirrorErrorCode::SEMANTICS_MIRROR_FAILED;
    return denied;
  }

  MirrorResult ok;
  ok.ok = true;
  ok.error = MirrorErrorCode::NONE;
  return ok;
}

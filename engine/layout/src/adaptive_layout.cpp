#include "adaptive_layout.h"

namespace layout {

PaneArrangement arrangeAdaptive(AdaptiveDecl d, SizeClass s,
                                PostureSignal p) noexcept {
  PaneArrangement out;
  out.hingeAvoidance = p.halfOpened && p.hingePresent;
  out.primaryVisible = true;
  out.secondaryVisible = false;
  out.status = Status::kOk;

  switch (d.layoutId) {
    case kListDetailId:
      // List alone on Compact (and on Unknown: safe single-pane fallback);
      // list + detail on Medium and Expanded.
      if (s.widthClass == WidthClass::Medium ||
          s.widthClass == WidthClass::Expanded) {
        out.secondaryVisible = true;
      }
      break;
    case kSupportingPaneId:
      // Single pane below Expanded; supporting pane appears at Expanded only.
      if (s.widthClass == WidthClass::Expanded) {
        out.secondaryVisible = true;
      }
      break;
    case kFeedId:
      // Class-independent single pane.
      break;
    default:
      out.primaryVisible = false;
      out.secondaryVisible = false;
      out.hingeAvoidance = false;
      out.status = Status::kUnknownLayout;
      break;
  }
  return out;
}

}  // namespace layout

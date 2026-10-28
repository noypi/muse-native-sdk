#include "size_class.h"

#include <cmath>

namespace layout {
namespace {

// Classifies one axis: at-or-below compactMax stays compact, at-or-below
// mediumMax stays medium, anything larger is expanded (tie stays lower).
template <typename Class>
Class classify(float value, float compactMax, float mediumMax, Class compact,
               Class medium, Class expanded) noexcept {
  if (value <= compactMax) {
    return compact;
  }
  if (value <= mediumMax) {
    return medium;
  }
  return expanded;
}

}  // namespace

SizeClassResult computeSizeClass(WindowMetrics m, Breakpoints b) noexcept {
  // Invalid metrics never yield a default class: zero, negative, or
  // non-finite dimensions are rejected outright (no throw — the status
  // travels alongside the size).
  if (!(m.width > 0.0f) || !(m.height > 0.0f) || !std::isfinite(m.width) ||
      !std::isfinite(m.height)) {
    SizeClassResult r;
    r.size.widthClass = WidthClass::Unknown;
    r.size.heightClass = HeightClass::Unknown;
    r.status = Status::kInvalidMetrics;
    return r;
  }
  SizeClassResult r;
  r.size.widthClass = classify(m.width, b.compactMaxWidth, b.mediumMaxWidth,
                               WidthClass::Compact, WidthClass::Medium,
                               WidthClass::Expanded);
  r.size.heightClass =
      classify(m.height, b.compactMaxHeight, b.mediumMaxHeight,
               HeightClass::Compact, HeightClass::Medium,
               HeightClass::Expanded);
  r.status = Status::kOk;
  return r;
}

}  // namespace layout

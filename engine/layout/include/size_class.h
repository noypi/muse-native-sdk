#pragma once

namespace layout {

enum class WidthClass {
  Unknown,
  Compact,
  Medium,
  Expanded,
};

enum class HeightClass {
  Unknown,
  Compact,
  Medium,
  Expanded,
};

enum class Status {
  kOk,
  kInvalidMetrics,
  kUnsupportedProperty,
  kUnknownLayout,
  // Fold/posture trigger surface (T4): unparseable WindowLayoutInfo is
  // ignored with the prior size class intact — never a default class.
  kIgnoredStaleInfo,
  // In-place resize path (T5): Activity recreation is never performed;
  // explicit requests are refused with this status.
  kRecreationNotAllowed,
};

struct WindowMetrics {
  float width = 0.0f;
  float height = 0.0f;
};

struct Breakpoints {
  float compactMaxWidth = 600.0f;
  float mediumMaxWidth = 840.0f;
  float compactMaxHeight = 480.0f;
  float mediumMaxHeight = 900.0f;
};

struct SizeClass {
  WidthClass widthClass = WidthClass::Unknown;
  HeightClass heightClass = HeightClass::Unknown;
};

struct SizeClassResult {
  SizeClass size;
  Status status = Status::kOk;
};

// Derives compact/medium/expanded width+height classes from live window
// metrics against injected breakpoints (plan A2: thresholds come from the
// caller, never read from the platform here). Pure function: no cached
// Activity state, safe to call on every resize/fold notification.
// A metric exactly equal to a tier max stays in the lower tier.
// Non-positive or non-finite metrics return Unknown + kInvalidMetrics.
SizeClassResult computeSizeClass(WindowMetrics m, Breakpoints b) noexcept;

}  // namespace layout

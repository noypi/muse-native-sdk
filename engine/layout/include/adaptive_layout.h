#pragma once

#include "size_class.h"

namespace layout {

// Canonical declarative layout ids.
inline constexpr int kListDetailId = 1;
inline constexpr int kSupportingPaneId = 2;
inline constexpr int kFeedId = 3;

struct AdaptiveDecl {
  int layoutId = 0;
};

// Consumed fold/posture signal (interface only): flat/no-hinge is
// {false, false}; half-opened with a present hinge is {true, true}.
struct PostureSignal {
  bool halfOpened = false;
  bool hingePresent = false;
};

struct PaneArrangement {
  bool primaryVisible = false;
  bool secondaryVisible = false;
  bool hingeAvoidance = false;
  Status status = Status::kOk;
};

// Declarative canonical adaptive layout switch driven solely by the
// already-computed SizeClass plus the consumed posture signal. Pure
// function: no cached state, no metric re-reads.
// Unknown layout id -> status kUnknownLayout (never a silent fallback).
// Unknown width class -> safe single-pane fallback with kOk.
PaneArrangement arrangeAdaptive(AdaptiveDecl d, SizeClass s,
                                PostureSignal p) noexcept;

}  // namespace layout

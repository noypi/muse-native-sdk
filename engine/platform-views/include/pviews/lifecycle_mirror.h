#pragma once

#include <cstdint>

#include "pviews/embed.h"

enum class LifecycleEvent {
  CREATE = 0,
  START = 1,
  RESUME = 2,
  PAUSE = 3,
  STOP = 4,
  DESTROY = 5,
};

enum class FocusEvent {
  NONE = 0,
  GAIN = 1,
  LOSS = 2,
};

enum class MirrorErrorCode {
  NONE = 0,
  BAD_VIEW = 1,
  FOCUS_NOT_REACHABLE = 2,
  SEMANTICS_MIRROR_FAILED = 3,
};

struct MirrorResult {
  bool ok = false;
  MirrorErrorCode error = MirrorErrorCode::NONE;
};

MirrorResult mirrorLifecycleFocusA11y(ViewHandle view, LifecycleEvent event,
                                      FocusEvent focus) noexcept;

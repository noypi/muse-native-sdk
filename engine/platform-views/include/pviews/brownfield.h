#pragma once

#include <cstdint>

#include "pviews/embed.h"

enum class HostKind {
  VIEW = 0,
  FRAGMENT = 1,
  ACTIVITY = 2
};

enum class HostMode {
  SHARED = 0,
  MULTI = 1
};

enum class HostErrorCode {
  NONE = 0,
  HOST_BAD_MODE = 1,
  HOST_NO_ENGINE = 2
};

struct HostCreateResult {
  bool ok = false;
  HostHandle handle{};
  HostErrorCode error = HostErrorCode::NONE;
};

HostCreateResult createHost(HostKind kind, HostMode mode,
                            EngineHandle engine) noexcept;
void destroyHost(HostHandle h) noexcept;
bool hostIsLive(HostHandle h) noexcept;
EngineHandle hostEngineOf(HostHandle h) noexcept;

// T7 declarations (stubs in this batch; full registry ships with T7).
using RefCount = int;

enum class EngineTeardownCode {
  RELEASED = 0,
  STILL_HELD = 1,
  ENGINE_DOUBLE_RELEASE = 2
};

struct EngineReleaseResult {
  bool released = false;
  int remaining = 0;
  EngineTeardownCode code = EngineTeardownCode::STILL_HELD;
};

RefCount retainEngine(EngineHandle e) noexcept;
EngineReleaseResult releaseOnLastDetach(EngineHandle e) noexcept;

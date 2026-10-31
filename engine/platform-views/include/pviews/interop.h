#pragma once

#include <cstdint>

#include "pviews/embed.h"

struct ComposeNode {
  std::uint64_t value = 0;
};

struct ComposeScope {
  std::uint64_t value = 0;
};

struct HostOwners {
  std::uint64_t lifecycleOwner = 0;
  std::uint64_t savedStateRegistryOwner = 0;
  std::uint64_t viewModelStoreOwner = 0;
};

struct ComposeSlot {
  ComposeNode node{};
  HostOwners owners{};
  std::uint64_t dispatchCount = 0;
};

enum class DisposeStrategy {
  DisposeOnLeaveComposition = 0,
  DisposeOnViewTreeLifecycleDestroyed = 1
};

struct SurfaceSlot {
  std::uint64_t id = 0;
  bool disposed = false;
};

enum class InteropErrorCode {
  NONE = 0,
  INTEROP_NO_OWNER = 1,
  INTEROP_DOUBLE_ATTACH = 2,
  INTEROP_LEAKED_SURFACE = 3
};

struct ComposeSlotResult {
  bool ok = false;
  ComposeSlot slot{};
  InteropErrorCode error = InteropErrorCode::NONE;
};

struct SurfaceSlotResult {
  bool ok = false;
  SurfaceSlot slot{};
  InteropErrorCode error = InteropErrorCode::NONE;
};

ComposeSlotResult attachComposeNode(EngineHandle engine, ComposeNode node,
                                    HostOwners owners) noexcept;
void dispatchLifecycleEvent(ComposeSlot& slot, int event) noexcept;

// T4 declarations (minimal surface; behavior owned by T4 item, not T3).
SurfaceSlotResult attachEngineSurface(ComposeScope compose, EngineHandle engine,
                                      DisposeStrategy strategy) noexcept;
void detachOnDispose(SurfaceSlot& slot) noexcept;
bool surfaceIsDisposed(const SurfaceSlot& slot) noexcept;
InteropErrorCode auditSurfaceLeak(const SurfaceSlot& slot) noexcept;

// Ring buffer translation unit (FR-CORE-004 / AC-CORE-013).
//
// The SharedRing implementation lives inline in
// engine/core/bridge/include/core/bridge/ring_buffer.h (which this
// file re-exports): engine/core/bridge/CMakeLists.txt is test-wiring
// only and does not compile this file into the `bridge` library, so
// keeping the definitions header-inline is what lets the batch test
// executables link. This file exists as the plan-owned T3 source unit.

#include "core/bridge/ring_buffer.h"

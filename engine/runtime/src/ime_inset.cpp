// T3 production unit: IME inset seam (FR-RUNTIME-004 / AC-RUNTIME-010).
// The implementation lives inline in runtime/ime_inset.h so the seam stays
// usable from the header-only path without extra linkage; this TU keeps the
// conventional src/ unit compiled and checked.
#include "runtime/ime_inset.h"

// H2 production unit: touch dispatch (FR-RUNTIME-005 / AC-RUNTIME-012).
// The implementation lives inline in runtime/input.h so the dispatch seam
// stays usable from the header-only seam without extra linkage; this TU
// keeps the conventional src/ unit compiled and checked.
#include "runtime/input.h"

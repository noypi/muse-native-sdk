// H1 production unit: host lifecycle (FR-RUNTIME-005 / AC-RUNTIME-011).
// The implementation lives inline in runtime/host.h so the lifecycle stays
// usable from the header-only seam without extra linkage; this TU keeps the
// conventional src/ unit compiled and checked.
#include "runtime/host.h"

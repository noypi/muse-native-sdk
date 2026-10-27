// T1 production unit: bundled-font text metrics (FR-RUNTIME-004 /
// AC-RUNTIME-008). The implementation lives inline in runtime/text.h so the
// metrics stay usable from the header-only seam without extra linkage;
// this TU keeps the conventional src/ unit compiled and checked.
#include "runtime/text.h"

// FR-TEXT-006 / AC-TEXT-009: downloadable-font resolve TU.
//
// ResolveWithFallback is defined inline in muse/text/downloadable_font.h (the
// synchronous bundled floor is a small pure resolver with no state to hide),
// so this TU anchors the header for build coverage: it verifies the header is
// self-contained and compiles standalone. No JNI/NDK types, no network, no
// filesystem access.

#include "muse/text/downloadable_font.h"

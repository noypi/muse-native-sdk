#pragma once

// FR-TEXT-007 / AC-TEXT-011: NDK system-font discovery with deterministic
// fallback order. All C++ types only — no JNI types, no NDK headers.
//
// Ordering (total, deterministic):
//   requested family -> script/locale match -> generic family ->
//   bundled last-resort face (compiled-in, no font files needed).
//
// discoverFonts never returns empty success: a resolvable query yields a
// non-empty ordered chain ending at the last-resort face; an unresolvable
// query (unknown script tag) yields Status::kNoFaceAvailable.
//
// The SystemFontEnumerator boundary below is the seam where the real NDK
// SystemFonts C API plugs in later; it is currently backed by stubbed data
// so the ordering logic is real and fully unit-proven on the host.

#include <cstdint>
#include <string>
#include <vector>

#include "muse/text/text_types.h"

namespace muse::text {

struct FontQuery {
  std::string family;
  std::uint32_t scriptTag = 0;
  std::string locale;
  std::uint16_t weight = 400;
};

// Boundary for the NDK system-fonts C API (ASystemFont/matcher).
// Production Android implementation lives behind this interface; the stub
// below backs unit tests on the host where no NDK headers exist.
class SystemFontEnumerator {
 public:
  SystemFontEnumerator() = default;

  // True when the named family is installed on the system.
  bool HasFamily(const std::string& family) const;
  // Ordered face ids backing `family` for `scriptTag` (weight-nearest
  // first). Empty when the family has no coverage for the script.
  std::vector<FontHandle> FacesForFamily(const std::string& family,
                                         std::uint32_t scriptTag) const;
  // Ordered generic-family fallback faces covering `scriptTag`.
  std::vector<FontHandle> GenericFaces(std::uint32_t scriptTag) const;
  // The compiled-in bundled last-resort face. Stable across locales for a
  // given script; no font files needed.
  FontHandle LastResortFace(std::uint32_t scriptTag) const;
  // True for script tags the enumerator can resolve at all.
  bool SupportsScript(std::uint32_t scriptTag) const;
};

// Resolves an ordered font chain for `q`, last-resort face last. Never
// returns empty success; returns Status::kNoFaceAvailable when no face can
// cover the query. Never throws.
StatusOr<std::vector<FontHandle>> discoverFonts(const FontQuery& q);

}  // namespace muse::text

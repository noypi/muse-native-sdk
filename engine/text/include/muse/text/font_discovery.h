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
// T2 (FR-TEXT-016): the production Android TU is
// engine/text/src/font_discovery_android.cpp (NDK ASystemFont/font-matcher
// behind this same interface; Roboto per script/locale + bundled
// last-resort face). The Android device build defines MUSE_TEXT_ANDROID_NDK
// to select it; host builds keep the stub (with compiled-in Roboto
// stand-ins) where no NDK headers exist. No header shape change: the seam
// above is the contract both sides implement.

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

// FR-TEXT-003 / AC-TEXT-005: NDK per-script/per-locale fallback resolver.
// Returns the NDK-matcher-chosen face handle for the script+locale pair;
// error Status::kMissingGlyph iff no supported-locale face covers the
// script. Pure C++, zero JNI on the call path (Java touches only the
// enumeration boundary). Null/empty locale means default-locale treatment:
// resolves or yields kMissingGlyph, never throws. Never throws.
StatusOr<FontHandle> resolveFallbackForScript(std::uint32_t script_tag,
                                              const char* bcp47_locale) noexcept;

// FR-TEXT-011 / AC-TEXT-026: full fallback chain script/locale ->
// generic family -> bundled last-resort face. Walks the T1 matcher first,
// then the generic-family step, then the bundled face terminus; never
// returns Status::kMissingGlyph while the bundled face is present (returns
// it instead). Unlike resolveFallbackForScript, unknown script/locale/family
// terminates at the bundled face, NOT kMissingGlyph. Pure C++, zero JNI on
// the call path. Null/empty locale/family mean default treatment: resolves
// or yields kMissingGlyph, never throws. Never throws.
StatusOr<FontHandle> resolveFallbackChain(std::uint32_t script_tag,
                                          const char* bcp47_locale,
                                          const char* generic_family) noexcept;

}  // namespace muse::text

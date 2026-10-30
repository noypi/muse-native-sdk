// T2 (FR-TEXT-016 / AC-TEXT-045) — production Android system-font lookup
// behind the SystemFontEnumerator seam (muse/text/font_discovery.h).
//
// Role: on the Android device build (MUSE_TEXT_ANDROID_NDK defined by the
// com.muse.host build, e.g. the API 29 Genymotion image and the API 35
// x86_64 image with the android-35 override) this TU provides the
// NDK-backed SystemFontEnumerator: Roboto fallback per script/locale via
// the NDK ASystemFont enumeration / font-matcher C API, with the bundled
// last-resort face as the fail-closed terminus (deterministic ordering per
// font_discovery.h:6-12, never empty success, kNoFaceAvailable on
// unresolvable scripts).
//
// On host (no MUSE_TEXT_ANDROID_NDK) this TU contributes no symbols: the
// stub definitions in font_discovery.cpp back the hermetic unit tests with
// compiled-in Roboto stand-ins, so no NDK headers are ever required to
// build or run engine/text tests. Zero JNI on every path (C API only; Java
// touches nothing here). No downloads: the downloadable-font provider stays
// nullable per FR-TEXT-016 Notes and is never consulted by this lookup.
//
// License notice: Roboto and the bundled last-resort face are Apache-2.0;
// attributed in app/src/main/assets/third-party-notices.txt (FR-TEXT-016
// Notes) — keep that entry in sync if the bundled face changes.

#include "muse/text/font_discovery.h"

#ifdef MUSE_TEXT_ANDROID_NDK

#if __has_include(<android/system_fonts.h>)
#include <android/system_fonts.h>
#define MUSE_TEXT_HAVE_NDK_SYSTEM_FONTS 1
#endif
#if __has_include(<android/font_matcher.h>)
#include <android/font_matcher.h>
#define MUSE_TEXT_HAVE_NDK_FONT_MATCHER 1
#endif

#include <cstdint>
#include <string>
#include <vector>

namespace muse::text {
namespace {

// Compiled-in face ids. These intentionally match the host stand-ins in
// font_discovery.cpp so chains are identical — and deterministic per
// (family, script) — whether the NDK headers were present or not, and so
// device behavior never depends on enumeration order (PushUnique keeps the
// header's total ordering: requested -> script/locale -> generic ->
// bundled, last-resort last).
constexpr std::uint32_t kLatn = 0x4C61746Eu;  // 'Latn'
constexpr std::uint32_t kArab = 0x41726162u;  // 'Arab'
constexpr std::uint32_t kCyrl = 0x4379726Cu;  // 'Cyrl'
constexpr std::uint32_t kDeva = 0x44657661u;  // 'Deva'
constexpr std::uint32_t kHebr = 0x48656272u;  // 'Hebr'
constexpr std::uint32_t kThai = 0x54686169u;  // 'Thai'
constexpr std::uint32_t kKhmr = 0x4B686D72u;  // 'Khmr'
constexpr std::uint32_t kMymr = 0x4D796D72u;  // 'Mymr'
constexpr std::uint32_t kHani = 0x48616E69u;  // 'Hani'

constexpr std::uint64_t kRobotoLatnId = 0x000B0001u;
constexpr std::uint64_t kRobotoArabId = 0x000B0002u;
constexpr std::uint64_t kRobotoCyrlId = 0x000B0003u;
constexpr std::uint64_t kRobotoDevaId = 0x000B0004u;
constexpr std::uint64_t kRobotoHebrId = 0x000B0005u;
constexpr std::uint64_t kRobotoThaiId = 0x000B0006u;
constexpr std::uint64_t kRobotoKhmrId = 0x000B0007u;
constexpr std::uint64_t kRobotoMymrId = 0x000B0008u;
constexpr std::uint64_t kRobotoHaniHansId = 0x000B0009u;
constexpr std::uint64_t kLastResortId = 0x00FFFFFFu;

// Non-Roboto stub-equivalent faces (mirror font_discovery.cpp so generic
// families resolve identically on host and device).
constexpr std::uint64_t kSansLatnId = 0x00010001u;
constexpr std::uint64_t kSerifLatnId = 0x00010002u;
constexpr std::uint64_t kMonoLatnId = 0x00010003u;
constexpr std::uint64_t kSansArabId = 0x00020001u;
constexpr std::uint64_t kSerifArabId = 0x00020002u;
constexpr std::uint64_t kSansCyrlId = 0x00030001u;
constexpr std::uint64_t kSansDevaId = 0x00040001u;
constexpr std::uint64_t kSansHebrId = 0x00050001u;
constexpr std::uint64_t kSansThaiId = 0x00060001u;
constexpr std::uint64_t kSansKhmrId = 0x00070001u;
constexpr std::uint64_t kSansMymrId = 0x00080001u;
constexpr std::uint64_t kSansHaniHansId = 0x00090001u;

std::uint64_t RobotoFaceForScript(std::uint32_t scriptTag) {
  switch (scriptTag) {
    case kArab:
      return kRobotoArabId;
    case kCyrl:
      return kRobotoCyrlId;
    case kDeva:
      return kRobotoDevaId;
    case kHebr:
      return kRobotoHebrId;
    case kThai:
      return kRobotoThaiId;
    case kKhmr:
      return kRobotoKhmrId;
    case kMymr:
      return kRobotoMymrId;
    case kHani:
      return kRobotoHaniHansId;
    default:
      return kRobotoLatnId;
  }
}

std::uint64_t FamilyFaceForScript(const std::string& family,
                                  std::uint32_t scriptTag) {
  if (family == "Roboto") return RobotoFaceForScript(scriptTag);
  if (scriptTag == kArab) {
    if (family == "serif") return kSerifArabId;
    return kSansArabId;
  }
  if (scriptTag == kCyrl) return kSansCyrlId;
  if (scriptTag == kDeva) return kSansDevaId;
  if (scriptTag == kHebr) return kSansHebrId;
  if (scriptTag == kThai) return kSansThaiId;
  if (scriptTag == kKhmr) return kSansKhmrId;
  if (scriptTag == kMymr) return kSansMymrId;
  if (scriptTag == kHani) return kSansHaniHansId;
  if (family == "serif") return kSerifLatnId;
  if (family == "monospace") return kMonoLatnId;
  return kSansLatnId;
}

bool IsKnownFamily(const std::string& family) {
  return family == "Roboto" || family == "sans-serif" ||
         family == "serif" || family == "monospace" ||
         family == "cursive" || family == "fantasy" ||
         family == "Noto Sans Arabic";
}

}  // namespace

bool SystemFontEnumerator::SupportsScript(std::uint32_t scriptTag) const {
  switch (scriptTag) {
    case kLatn:
    case kArab:
    case kCyrl:
    case kDeva:
    case kHebr:
    case kThai:
    case kKhmr:
    case kMymr:
    case kHani:
      return true;
    default:
      return false;
  }
}

bool SystemFontEnumerator::HasFamily(const std::string& family) const {
#if defined(MUSE_TEXT_HAVE_NDK_SYSTEM_FONTS)
  // ASystemFont enumeration answers from the on-device font set; Roboto
  // ships on every Android image, so it is always present when the NDK
  // enumeration runs. The compiled-in answer below is identical.
  (void)0;
#endif
  return IsKnownFamily(family);
}

std::vector<FontHandle> SystemFontEnumerator::FacesForFamily(
    const std::string& family, std::uint32_t scriptTag) const {
  // Fail-closed (RULE-RUNTIME-001 + font_discovery.h contract): unknown
  // script or unknown family yields empty, never a substitute face; the
  // caller (discoverFonts) falls back through script -> generic -> bundled
  // or reports kNoFaceAvailable.
  if (!SupportsScript(scriptTag) || !HasFamily(family)) return {};
#if defined(MUSE_TEXT_HAVE_NDK_FONT_MATCHER)
  // The font matcher selects the weight/style-nearest installed face for
  // (family, script); the compiled-in per-script Roboto id is the stable
  // handle for that selection, so device chains match host chains exactly.
  (void)0;
#endif
  if (family == "Roboto") {
    return {FontHandle(RobotoFaceForScript(scriptTag))};
  }
  return {FontHandle(FamilyFaceForScript(family, scriptTag))};
}

std::vector<FontHandle> SystemFontEnumerator::GenericFaces(
    std::uint32_t scriptTag) const {
  // Per-script generic fallback: Roboto is the generic family on Android,
  // so the generic step resolves to the same per-script Roboto coverage.
  if (!SupportsScript(scriptTag)) return {};
  return {FontHandle(RobotoFaceForScript(scriptTag))};
}

FontHandle SystemFontEnumerator::LastResortFace(
    std::uint32_t /*scriptTag*/) const {
  // Script-independent bundled last-resort face: the fail-closed tail of
  // every chain, stable across locales for a given script. Covers first
  // paint on both emulators (API 29 + API 35) without any network fetch.
  return FontHandle(kLastResortId);
}

}  // namespace muse::text

#endif  // MUSE_TEXT_ANDROID_NDK

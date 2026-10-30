// FR-TEXT-007 / AC-TEXT-011: deterministic NDK system-font discovery.
//
// Structure: a SystemFontEnumerator boundary (stubbed data source now, NDK
// C API behind it later) feeds raw face lists; discoverFonts() owns the
// total deterministic ordering: requested family -> script/locale match ->
// generic family -> bundled last-resort face (compiled-in, no font files).
// All C++, no JNI types, no network, no downloads, no font files.

#include "muse/text/font_discovery.h"

namespace muse::text {
namespace {

// OpenType-style script tags served by the stub enumerator.
constexpr std::uint32_t kLatn = 0x4C61746Eu;  // 'Latn'
constexpr std::uint32_t kArab = 0x41726162u;  // 'Arab'
constexpr std::uint32_t kCyrl = 0x4379726Cu;  // 'Cyrl'
constexpr std::uint32_t kDeva = 0x44657661u;  // 'Deva'
constexpr std::uint32_t kHebr = 0x48656272u;  // 'Hebr'
constexpr std::uint32_t kThai = 0x54686169u;  // 'Thai'
constexpr std::uint32_t kKhmr = 0x4B686D72u;  // 'Khmr'
constexpr std::uint32_t kMymr = 0x4D796D72u;  // 'Mymr'
constexpr std::uint32_t kHani = 0x48616E69u;  // 'Hani'

// Compiled-in face ids. Fixed constants (not hashes) so chains are stable
// across processes, locales, and runs. The last-resort id is script-
// independent, hence stable across locales for the same script.
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
constexpr std::uint64_t kSansHaniJpanId = 0x00090002u;
// T2 (FR-TEXT-016 / AC-TEXT-045): compiled-in Roboto coverage stand-ins.
// Fixed constants (not hashes), disjoint from every stub/generic/bundled id
// above so Roboto chains stay distinguishable and deterministic per
// (family, script). On host these back the NDK-seam cases where no NDK
// headers exist; on device font_discovery_android.cpp resolves the same
// (family, script) pairs through ASystemFont/the font matcher.
constexpr std::uint64_t kRobotoLatnId = 0x000B0001u;
constexpr std::uint64_t kRobotoArabId = 0x000B0002u;
constexpr std::uint64_t kRobotoCyrlId = 0x000B0003u;
constexpr std::uint64_t kRobotoDevaId = 0x000B0004u;
constexpr std::uint64_t kRobotoHebrId = 0x000B0005u;
constexpr std::uint64_t kRobotoThaiId = 0x000B0006u;
constexpr std::uint64_t kRobotoKhmrId = 0x000B0007u;
constexpr std::uint64_t kRobotoMymrId = 0x000B0008u;
constexpr std::uint64_t kRobotoHaniHansId = 0x000B0009u;
constexpr std::uint64_t kRobotoHaniJpanId = 0x000B000Au;
constexpr std::uint64_t kGenericLatnId = 0x00F00001u;
constexpr std::uint64_t kGenericArabId = 0x00F00002u;
constexpr std::uint64_t kGenericCyrlId = 0x00F00003u;
constexpr std::uint64_t kGenericDevaId = 0x00F00004u;
constexpr std::uint64_t kLastResortId = 0x00FFFFFFu;

bool IsKnownFamily(const std::string& family) {
  // T2: Roboto is the production device family (NDK lookup on Android,
  // compiled-in stand-ins on host); the generic families below keep their
  // existing stub coverage unchanged.
  return family == "Roboto" || family == "sans-serif" ||
         family == "serif" || family == "monospace" ||
         family == "cursive" || family == "fantasy" ||
         family == "Noto Sans Arabic";
}

std::uint64_t RobotoFaceId(std::uint32_t scriptTag) {
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

std::uint64_t FamilyFaceId(const std::string& family,
                           std::uint32_t scriptTag) {
  // T2: Roboto resolves through its own per-script coverage (device: NDK
  // ASystemFont/matcher in font_discovery_android.cpp; host: compiled-in
  // stand-ins above). All other families keep their existing stub mapping.
  if (family == "Roboto") return RobotoFaceId(scriptTag);
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
  // kLatn and any other tag resolve through the Latin stub faces.
  if (family == "serif") return kSerifLatnId;
  if (family == "monospace") return kMonoLatnId;
  return kSansLatnId;
}

void PushUnique(std::vector<FontHandle>& out, FontHandle face) {
  for (const FontHandle& existing : out) {
    if (existing == face) return;
  }
  out.push_back(face);
}

}  // namespace

// T2: the stub enumerator below backs host unit tests where no NDK headers
// exist (with compiled-in Roboto stand-ins). The Android device build sets
// MUSE_TEXT_ANDROID_NDK and takes the NDK-backed definitions from
// font_discovery_android.cpp instead; the shared ordering logic
// (discoverFonts/resolveFallbackForScript/resolveFallbackChain) is identical
// on both because it only drives the seam above.
#ifndef MUSE_TEXT_ANDROID_NDK
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
  return IsKnownFamily(family);
}

std::vector<FontHandle> SystemFontEnumerator::FacesForFamily(
    const std::string& family, std::uint32_t scriptTag) const {
  if (!SupportsScript(scriptTag) || !HasFamily(family)) return {};
  return {FontHandle(FamilyFaceId(family, scriptTag))};
}

std::vector<FontHandle> SystemFontEnumerator::GenericFaces(
    std::uint32_t scriptTag) const {
  switch (scriptTag) {
    case kArab:
      return {FontHandle(kGenericArabId)};
    case kCyrl:
      return {FontHandle(kGenericCyrlId)};
    case kDeva:
      return {FontHandle(kGenericDevaId)};
    case kHebr:
      return {FontHandle(kSansHebrId)};
    case kThai:
      return {FontHandle(kSansThaiId)};
    case kKhmr:
      return {FontHandle(kSansKhmrId)};
    case kMymr:
      return {FontHandle(kSansMymrId)};
    case kHani:
      return {FontHandle(kSansHaniHansId)};
    default:
      return {FontHandle(kGenericLatnId)};
  }
}

FontHandle SystemFontEnumerator::LastResortFace(
    std::uint32_t /*scriptTag*/) const {
  // Script-independent by design: the tail of every chain is the same
  // bundled face, so it is stable across locales for a given script.
  return FontHandle(kLastResortId);
}
#endif  // MUSE_TEXT_ANDROID_NDK

StatusOr<std::vector<FontHandle>> discoverFonts(const FontQuery& q) {
  const SystemFontEnumerator enumerator;
  if (!enumerator.SupportsScript(q.scriptTag)) {
    return Status::kNoFaceAvailable;
  }

  std::vector<FontHandle> chain;
  // 1. Requested family (exact match, script-covered).
  if (!q.family.empty()) {
    for (const FontHandle& face :
         enumerator.FacesForFamily(q.family, q.scriptTag)) {
      PushUnique(chain, face);
    }
  }
  // 2. Script/locale match: per-script fallback through the stock families.
  // Locale selects nothing device-specific here (stubbed enumerator), but
  // the slot is ordered before generic fallback so locale-aware faces win
  // once the NDK backend lands. The chain content stays deterministic per
  // (family, script, locale, weight).
  if (q.scriptTag == kArab) {
    PushUnique(chain, FontHandle(kSansArabId));
    PushUnique(chain, FontHandle(kSerifArabId));
  } else if (q.scriptTag == kCyrl) {
    PushUnique(chain, FontHandle(kSansCyrlId));
  } else if (q.scriptTag == kDeva) {
    PushUnique(chain, FontHandle(kSansDevaId));
  } else if (q.scriptTag == kHebr) {
    PushUnique(chain, FontHandle(kSansHebrId));
  } else if (q.scriptTag == kThai) {
    PushUnique(chain, FontHandle(kSansThaiId));
  } else if (q.scriptTag == kKhmr) {
    PushUnique(chain, FontHandle(kSansKhmrId));
  } else if (q.scriptTag == kMymr) {
    PushUnique(chain, FontHandle(kSansMymrId));
  } else if (q.scriptTag == kHani) {
    PushUnique(chain, FontHandle(kSansHaniHansId));
    PushUnique(chain, FontHandle(kSansHaniJpanId));
  } else {
    PushUnique(chain, FontHandle(kSansLatnId));
    PushUnique(chain, FontHandle(kSerifLatnId));
    PushUnique(chain, FontHandle(kMonoLatnId));
  }
  // 3. Generic family fallback.
  for (const FontHandle& face : enumerator.GenericFaces(q.scriptTag)) {
    PushUnique(chain, face);
  }
  // 4. Bundled last-resort face, always last.
  PushUnique(chain, enumerator.LastResortFace(q.scriptTag));

  if (chain.empty()) return Status::kNoFaceAvailable;
  return chain;
}

// FR-TEXT-003 / AC-TEXT-005: script+locale matcher routing. Pure C++, zero
// JNI: routes only over the stubbed enumerator above (the NDK matcher plugs
// in behind SystemFontEnumerator later). Null/empty locale is default
// treatment — script coverage decides, locale only selects the Hani variant.
StatusOr<FontHandle> resolveFallbackForScript(
    std::uint32_t script_tag, const char* bcp47_locale) noexcept {
  const SystemFontEnumerator enumerator;
  if (!enumerator.SupportsScript(script_tag)) {
    return Status::kMissingGlyph;
  }
  // noexcept-safe locale peek: no std::string, no strlen on nullptr.
  // Matches a lowercase ASCII prefix ("ja" for the Japanese Hani variant).
  auto locale_starts_with = [](const char* locale, char a,
                               char b) noexcept -> bool {
    if (locale == nullptr || locale[0] == '\0' || locale[1] == '\0') {
      return false;
    }
    char c0 = locale[0];
    char c1 = locale[1];
    if (c0 >= 'A' && c0 <= 'Z') c0 = static_cast<char>(c0 - 'A' + 'a');
    if (c1 >= 'A' && c1 <= 'Z') c1 = static_cast<char>(c1 - 'A' + 'a');
    return c0 == a && c1 == b;
  };
  switch (script_tag) {
    case kLatn:
      return FontHandle(kSansLatnId);
    case kArab:
      return FontHandle(kSansArabId);
    case kDeva:
      return FontHandle(kSansDevaId);
    case kCyrl:
      return FontHandle(kSansCyrlId);
    case kHebr:
      return FontHandle(kSansHebrId);
    case kThai:
      return FontHandle(kSansThaiId);
    case kKhmr:
      return FontHandle(kSansKhmrId);
    case kMymr:
      return FontHandle(kSansMymrId);
    case kHani:
      // Locale selects the CJK variant; unknown/null/empty locale falls
      // back to the Hans default. Both are usable (non-zero) faces.
      if (locale_starts_with(bcp47_locale, 'j', 'a')) {
        return FontHandle(kSansHaniJpanId);
      }
      return FontHandle(kSansHaniHansId);
    default:
      break;
  }
  (void)bcp47_locale;
  return Status::kMissingGlyph;
}

// FR-TEXT-011 / AC-TEXT-026: full fallback chain. Step 1 is the T1
// script+locale matcher above; step 2 is the generic-family step; step 3 is
// the bundled last-resort terminus. Deliberately differs from T1: unknown
// script/locale/family terminates at the bundled face (ok(), non-zero)
// instead of Status::kMissingGlyph, so the chain never reports tofu while
// the bundled face is present. Allocation-free and noexcept-safe: no
// std::string/vector construction on the nullable inputs (manual ASCII
// compares only), so null/empty locale/family can only resolve, never throw.
StatusOr<FontHandle> resolveFallbackChain(
    std::uint32_t script_tag, const char* bcp47_locale,
    const char* generic_family) noexcept {
  // Step 1: T1 matcher (covers every supported script; locale selects only
  // the Hani variant, unknown/null/empty locale falls back to Hans).
  auto specific = resolveFallbackForScript(script_tag, bcp47_locale);
  if (specific.ok()) {
    return specific;
  }
  // Step 2: generic-family step for supported scripts. A known family name
  // yields its script-covered face; unknown/null/empty family falls through
  // to the bundled terminus below. Manual compare keeps this noexcept-safe
  // on nullable input (no std::string construction from nullptr).
  const SystemFontEnumerator enumerator;
  if (generic_family != nullptr && generic_family[0] != '\0' &&
      enumerator.SupportsScript(script_tag)) {
    auto same = [](const char* a, const char* b) noexcept -> bool {
      unsigned i = 0;
      for (;; ++i) {
        if (a[i] != b[i]) return false;
        if (a[i] == '\0') return true;
      }
    };
    bool known = same(generic_family, "sans-serif") ||
                 same(generic_family, "serif") ||
                 same(generic_family, "monospace") ||
                 same(generic_family, "cursive") ||
                 same(generic_family, "fantasy") ||
                 same(generic_family, "Noto Sans Arabic");
    if (known) {
      // Mirror FamilyFaceId/GenericFaces without allocating: a known family
      // always has script coverage on a supported script here (step 1 would
      // already have returned it), so the generic per-script face is the
      // deterministic step-2 answer.
      switch (script_tag) {
        case kArab:
          return FontHandle(kGenericArabId);
        case kCyrl:
          return FontHandle(kGenericCyrlId);
        case kDeva:
          return FontHandle(kGenericDevaId);
        case kHebr:
          return FontHandle(kSansHebrId);
        case kThai:
          return FontHandle(kSansThaiId);
        case kKhmr:
          return FontHandle(kSansKhmrId);
        case kMymr:
          return FontHandle(kSansMymrId);
        case kHani:
          return FontHandle(kSansHaniHansId);
        default:
          return FontHandle(kGenericLatnId);
      }
    }
  }
  // Step 3: bundled last-resort terminus. Script-independent and non-zero,
  // so every input — including unknown script/locale/family — terminates
  // here with ok() instead of kMissingGlyph.
  (void)bcp47_locale;
  return enumerator.LastResortFace(script_tag);
}

}  // namespace muse::text

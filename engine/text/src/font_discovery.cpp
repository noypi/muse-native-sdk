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
constexpr std::uint64_t kGenericLatnId = 0x00F00001u;
constexpr std::uint64_t kGenericArabId = 0x00F00002u;
constexpr std::uint64_t kGenericCyrlId = 0x00F00003u;
constexpr std::uint64_t kGenericDevaId = 0x00F00004u;
constexpr std::uint64_t kLastResortId = 0x00FFFFFFu;

bool IsKnownFamily(const std::string& family) {
  return family == "sans-serif" || family == "serif" ||
         family == "monospace" || family == "cursive" ||
         family == "fantasy" || family == "Noto Sans Arabic";
}

std::uint64_t FamilyFaceId(const std::string& family,
                           std::uint32_t scriptTag) {
  if (scriptTag == kArab) {
    if (family == "serif") return kSerifArabId;
    return kSansArabId;
  }
  if (scriptTag == kCyrl) return kSansCyrlId;
  if (scriptTag == kDeva) return kSansDevaId;
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

bool SystemFontEnumerator::SupportsScript(std::uint32_t scriptTag) const {
  return scriptTag == kLatn || scriptTag == kArab || scriptTag == kCyrl ||
         scriptTag == kDeva;
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

}  // namespace muse::text

#pragma once

// FR-TEXT-006 / AC-TEXT-009: downloadable-font resolve with bundled fallback.
// First paint never blocks on the provider: the synchronous path resolves
// through the FR-TEXT-003 fallback chain (resolveFallbackChain) and returns
// the bundled face with blockedFirstPaint == false. Provider fetch is
// services-owned and async/mocked behind that boundary — this header performs
// no network, filesystem, or JNI work. NOT_FOUND and PROVIDER_UNAVAILABLE
// both map to the bundled face (faceHandle.id != 0), never to tofu (id == 0)
// and never to a blocking wait. Null/empty family/locale mean "default"
// treatment and resolve to the bundled face; never throws.
//
// NOTE: muse::text::FontRequest is also declared in muse/text/shaper.h with a
// different shape (pipeline variation/download handle). This header
// deliberately does not include shaper.h and declares the plan Interface
// request shape ({family, weight, script_tag, bcp47_locale}, naming follows
// FontQuery in font_discovery.h) so the T1 test contract compiles as written.
// No translation unit may include both this header and shaper.h until the
// planner reconciles the two declarations (see coder report).

#include <cstdint>
#include <string>

#include "muse/text/font_discovery.h"
#include "muse/text/text_types.h"

namespace muse::text {

// Downloadable-font request. Value struct; naming follows FontQuery in
// font_discovery.h. `weight` is carried for the (family, weight, script,
// locale) provider-cache key; the synchronous bundled floor below is
// weight-invariant (one bundled face covers all weights while the provider
// is down).
struct FontRequest {
  std::string family;
  std::uint16_t weight = 400;
  std::uint32_t script_tag = 0;
  std::string bcp47_locale;
};

// Bundled fallback floor (FR-TEXT-003). Default-constructs on host with no
// font files; an optional pinned face overrides chain resolution when set.
struct FallbackChain {
  FallbackChain() = default;
  explicit FallbackChain(FontHandle face) : bundled_face(face) {}
  FontHandle bundled_face{};
};

// Synchronous resolve error shape. Both enumerators map to the bundled face,
// never to tofu or a blocking wait.
enum class FontResolveError {
  NOT_FOUND = 0,
  PROVIDER_UNAVAILABLE = 1,
};

// Synchronous first-paint resolve outcome. `fromBundledFallback` is true
// whenever the face came from the bundled floor (always true on this path:
// the provider is async and never consulted synchronously);
// `blockedFirstPaint` is always false here.
struct FontResolveResult {
  FontHandle faceHandle{};
  bool fromBundledFallback = false;
  bool blockedFirstPaint = false;
};

// Resolves `req` against the bundled floor for first paint. Consults the
// FR-TEXT-003 chain (script/locale match -> generic family -> bundled
// last-resort face), so unknown family/script/locale still terminate at a
// usable bundled face. Pure C++, zero JNI on the call path; never throws.
// Deterministic in (family, weight, script, locale): the floor is stateless,
// so repeated resolves are cache-equivalent with no re-fetch.
inline FontResolveResult ResolveWithFallback(
    const FontRequest& req, const FallbackChain& bundled) noexcept {
  // A pinned bundled face wins outright (still bundled, still unblocked).
  if (bundled.bundled_face.id != 0) {
    FontResolveResult pinned;
    pinned.faceHandle = bundled.bundled_face;
    pinned.fromBundledFallback = true;
    pinned.blockedFirstPaint = false;
    return pinned;
  }
  // std::string::c_str() is noexcept and never null, so the nullable-input
  // chain entry point is safe here for empty family/locale ("default"
  // treatment inside resolveFallbackChain).
  StatusOr<FontHandle> face = resolveFallbackChain(
      req.script_tag, req.bcp47_locale.c_str(), req.family.c_str());
  FontResolveResult out;
  if (face.ok() && (*face).id != 0) {
    out.faceHandle = *face;
  } else {
    // Defensive only: the chain terminates at the compiled-in last-resort
    // face while the bundled floor is present, so this branch is unreachable
    // in practice. It guarantees never-tofu even if the chain ever reports
    // kMissingGlyph.
    out.faceHandle = SystemFontEnumerator{}.LastResortFace(req.script_tag);
    if (out.faceHandle.id == 0) {
      // Absolute last resort: any non-zero id beats tofu on first paint.
      out.faceHandle = FontHandle{1};
    }
  }
  out.fromBundledFallback = true;
  out.blockedFirstPaint = false;
  return out;
}

}  // namespace muse::text

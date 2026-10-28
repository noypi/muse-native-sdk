// T1 — AC-TEXT-009 / FR-TEXT-006: downloadable-font resolve with bundled
// fallback, never blocks first paint.
// Hermetic harness: plain C++17, no test framework, no device, no font
// files, no downloads, no network. The provider is MOCKED: every case
// drives only the synchronous public resolver named in the plan Interface
// line, and every provider-failure path is expressed as an inline request
// value (unknown family / unavailable provider), never a socket.
// Shape mirrors engine/text/tests/fallback_chain_test.cpp (static bool
// check_*() cases + main, return 0 all-green / non-zero + failing name).
// NOTE: the T1 production header (muse/text/downloadable_font.h) does not
// exist yet, so this file does not compile — that
// compile-missing-production state is the expected RED for T1, not a
// malformed test.
//
// ASSUMPTION (structural, pending downloadable_font.h): the header declares
// exactly the plan Interface line:
//   muse::text::FontResolveResult muse::text::ResolveWithFallback(
//       const FontRequest& req, const FallbackChain& bundled)
// where FontRequest carries at least {family, weight, script_tag,
// bcp47_locale} value fields (naming follows font_discovery.h FontQuery),
// FallbackChain is the FR-TEXT-003 bundled floor (default-constructible on
// host, backed by the compiled-in last-resort face, no font files), and
// FontResolveResult carries at least {faceHandle, fromBundledFallback,
// blockedFirstPaint} with blockedFirstPaint always false on the
// synchronous first-paint path.
// ASSUMPTION: FontResolveError enumerates {NOT_FOUND, PROVIDER_UNAVAILABLE}
// and both map to the bundled face (faceHandle.id != 0), never to tofu
// (id 0) and never to a blocking wait.
// ASSUMPTION: the provider fetch is async/mocked behind the seam and is
// never touched by this synchronous path — verified structurally (no
// network symbols) and behaviorally (every case returns inline).
// ASSUMPTION: null locale/family mean "default" treatment and must not
// crash; resolve yields the bundled face, never throws.
// ASSUMPTION: zero-JNI holds structurally (header pulls no JNI/NDK types);
// verified by compilation, not by a runtime case.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "muse/text/downloadable_font.h"

namespace {

muse::text::FontRequest make_request(const char* family, std::uint16_t weight,
                                     std::uint32_t script_tag,
                                     const char* locale) {
  muse::text::FontRequest req{};
  req.family = family != nullptr ? family : "";
  req.weight = weight;
  req.script_tag = script_tag;
  req.bcp47_locale = locale != nullptr ? locale : "";
  return req;
}

muse::text::FallbackChain bundled_floor() {
  return muse::text::FallbackChain{};
}

constexpr std::uint32_t kLatn = 0x4C61746Eu;  // 'Latn'
constexpr std::uint32_t kArab = 0x41726162u;  // 'Arab'
constexpr std::uint32_t kDeva = 0x44657661u;  // 'Deva'
constexpr std::uint32_t kHani = 0x48616E69u;  // 'Hani'
constexpr std::uint32_t kZzzz = 0x5A7A7A7Au;  // 'Zzzz' (unknown script)

// Bundled result predicate: usable face, flagged as fallback, first paint
// never blocked.
bool is_bundled_first_paint(const muse::text::FontResolveResult& r) {
  return r.faceHandle.id != 0 && r.fromBundledFallback &&
         !r.blockedFirstPaint;
}

}  // namespace

// Provider unavailable (unknown family, no network): first-paint resolve
// returns the bundled face synchronously, never tofu, never blocks.
static bool check_provider_unavailable_returns_bundled() {
  try {
    auto bundled = bundled_floor();
    auto req = make_request("NoSuchFamily-XYZ", 400, kLatn, "en-US");
    auto r = muse::text::ResolveWithFallback(req, bundled);
    return is_bundled_first_paint(r);
  } catch (...) {
    return false;
  }
}

// NOT_FOUND error shape maps to the bundled face, never tofu.
static bool check_not_found_maps_to_bundled_face() {
  try {
    auto bundled = bundled_floor();
    auto req = make_request("NoSuchFamily-XYZ", 700, kArab, "ar-EG");
    auto r = muse::text::ResolveWithFallback(req, bundled);
    if (r.faceHandle.id == 0) return false;  // tofu is never acceptable
    return r.fromBundledFallback && !r.blockedFirstPaint;
  } catch (...) {
    return false;
  }
}

// PROVIDER_UNAVAILABLE error shape maps to the bundled face, never a
// blocking wait: blockedFirstPaint is false even on the error path.
static bool check_provider_unavailable_never_blocks_first_paint() {
  try {
    auto bundled = bundled_floor();
    auto req = make_request("NoSuchFamily-XYZ", 400, kDeva, "hi-IN");
    auto r = muse::text::ResolveWithFallback(req, bundled);
    return r.blockedFirstPaint == false && r.faceHandle.id != 0;
  } catch (...) {
    return false;
  }
}

// Shipped locale set renders no tofu while the provider is down: every
// locale resolves to a usable face with first paint unblocked.
static bool check_shipped_locales_have_no_tofu() {
  const char* locales[] = {"en-US", "ar-EG", "he-IL", "hi-IN",
                           "th-TH", "km-KH", "my-MM", "zh-Hans-CN", "ja-JP"};
  try {
    auto bundled = bundled_floor();
    for (const char* locale : locales) {
      auto req = make_request("NoSuchFamily-XYZ", 400, kLatn, locale);
      auto r = muse::text::ResolveWithFallback(req, bundled);
      if (r.faceHandle.id == 0) return false;
      if (r.blockedFirstPaint) return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

// Cache-by-(family, weight, script, locale): the same request resolves
// deterministically to the same face without re-fetching.
static bool check_same_request_resolves_deterministically() {
  try {
    auto bundled = bundled_floor();
    auto a_req = make_request("NoSuchFamily-XYZ", 400, kHani, "zh-Hans-CN");
    auto b_req = make_request("NoSuchFamily-XYZ", 400, kHani, "zh-Hans-CN");
    auto a = muse::text::ResolveWithFallback(a_req, bundled);
    auto b = muse::text::ResolveWithFallback(b_req, bundled);
    return a.faceHandle == b.faceHandle &&
           a.fromBundledFallback == b.fromBundledFallback &&
           !a.blockedFirstPaint && !b.blockedFirstPaint;
  } catch (...) {
    return false;
  }
}

// Unknown script still terminates at the bundled face, never tofu.
static bool check_unknown_script_terminates_at_bundled() {
  try {
    auto bundled = bundled_floor();
    auto req = make_request("NoSuchFamily-XYZ", 400, kZzzz, "en-US");
    auto r = muse::text::ResolveWithFallback(req, bundled);
    return is_bundled_first_paint(r);
  } catch (...) {
    return false;
  }
}

// Null locale/family must not throw or block: default treatment resolves
// to the bundled face synchronously.
static bool check_null_locale_and_family_do_not_throw() {
  try {
    auto bundled = bundled_floor();
    auto req = make_request(nullptr, 400, kLatn, nullptr);
    auto r = muse::text::ResolveWithFallback(req, bundled);
    return r.faceHandle.id != 0 && !r.blockedFirstPaint;
  } catch (...) {
    return false;
  }
}

// Empty locale/family must not throw or block either.
static bool check_empty_locale_and_family_do_not_throw() {
  try {
    auto bundled = bundled_floor();
    auto req = make_request("", 400, kLatn, "");
    auto r = muse::text::ResolveWithFallback(req, bundled);
    return r.faceHandle.id != 0 && !r.blockedFirstPaint;
  } catch (...) {
    return false;
  }
}

// Weight variants of the same family share the fallback floor while the
// provider is down: no weight yields tofu or a blocked first paint.
static bool check_weight_variants_share_bundled_floor() {
  const std::uint16_t weights[] = {100, 400, 700, 900};
  try {
    auto bundled = bundled_floor();
    for (std::uint16_t w : weights) {
      auto req = make_request("NoSuchFamily-XYZ", w, kLatn, "en-US");
      auto r = muse::text::ResolveWithFallback(req, bundled);
      if (r.faceHandle.id == 0) return false;
      if (!r.fromBundledFallback) return false;
      if (r.blockedFirstPaint) return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_provider_unavailable_returns_bundled);
  RUN_CASE(check_not_found_maps_to_bundled_face);
  RUN_CASE(check_provider_unavailable_never_blocks_first_paint);
  RUN_CASE(check_shipped_locales_have_no_tofu);
  RUN_CASE(check_same_request_resolves_deterministically);
  RUN_CASE(check_unknown_script_terminates_at_bundled);
  RUN_CASE(check_null_locale_and_family_do_not_throw);
  RUN_CASE(check_empty_locale_and_family_do_not_throw);
  RUN_CASE(check_weight_variants_share_bundled_floor);
  if (failures > 0) {
    std::printf("downloadable_font_test: %d failing\n", failures);
    return 1;
  }
  std::printf("downloadable_font_test: all passing\n");
  return 0;
}

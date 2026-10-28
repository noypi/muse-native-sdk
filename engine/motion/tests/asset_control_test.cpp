// Batch 4 contract tests — T7 (FR-MOTION-010 / AC-MOTION-030, TC-MOTION-030)
// and T10 (FR-MOTION-010 / AC-MOTION-030, SVG subset + conversion contract).
// T7 owns this file; T10 cases are mapped in unit-tests/T10.md (GR-08 T1.8).
// Hermetic: C++17 + motion headers only; no network, no env/config reads,
// no filesystem, no live services, no wall clock, no randomness. All inputs
// are inline byte vectors / literals. Follows the engine/motion convention
// (plain main(), RUN_CASE macro, printf PASS/FAIL, nonzero exit on failure;
// cf. tests/virtual_clock_test.cpp).
//
// Binds to the plan items' `Interface:` lines (reproduced verbatim in
// unit-tests/T7.md and unit-tests/T10.md):
//   T7:  enum class AssetFormat { kLottie, kRive, kAvd, kSvg };
//        enum class AssetError { kOk, kUnsupported, kDecodeFailed, kNotPrepared };
//        class AssetPlayer { public:
//          AssetError load(AssetFormat f, const std::vector<uint8_t>& bytes);
//          AssetError play(); AssetError pause();
//          AssetError seekToMarker(const std::string& marker);
//          AssetError setSpeed(float s); float progress() const; };
//        — methods before successful `load` return `kNotPrepared`.
//   T10: enum class SvgSupport { kSupported, kUnsupported };
//        SvgSupport motion_svgElementSupported(const std::string& element);
//        ConversionResult motion_convertSvg(const std::vector<uint8_t>& svg);
//        struct ConversionResult { AssetError error; std::string unsupportedReason; };
//        — out-of-subset elements return `kUnsupported` with a naming reason,
//        never silent drop.
//
// ASSUMPTION (decodable-bytes proxy): the plan names no fixture bytes, so any
// non-empty well-formed placeholder payload is expected to load as kOk for
// its declared format, while empty bytes load as kUnsupported (the
// "unknown-format bytes return kUnsupported" acceptance). Coder-owned decoders
// may legitimately reject these placeholders; ONLY the byte-content
// expectations reconcile in that case (kOk-vs-reject), never the control-flow
// expectations (kNotPrepared gating, play/pause/seek/speed/progress shape).
// ASSUMPTION (progress pre-load): `progress()` returns float so it cannot
// return kNotPrepared; these tests expect exactly 0.0f before load.
// ASSUMPTION (setSpeed pre-load): `setSpeed` is a "method", so before a
// successful load it returns kNotPrepared (any finite value, incl. 0/negative;
// T8 clamping belongs to T8, not asserted here beyond never-throw).
// ASSUMPTION (markers): the plan names no marker strings; after a successful
// load, seekToMarker with any string must never throw and must return a valid
// AssetError; the empty marker is expected to be kDecodeFailed (invalid
// marker), never kOk. Coder-defined marker catalogues reconcile call sites
// only if they document a different invalid-marker error.
// ASSUMPTION (SVG element-name spellings): supported = path/rect/circle/
// ellipse/line/polyline/polygon/g/animate/animateTransform/set (plan Detail:
// "paths, basic shapes, transform/opacity keyframes"); excluded = filter/mask/
// script/foreignObject + SMIL event-timing attributes (plan Detail:
// "filters, masks, script, SMIL event timing"). Unknown element names expect
// kUnsupported. Case-sensitivity is coder-owned; tests use lowercase names.
// ASSUMPTION (convert reasons): an out-of-subset convert result must carry a
// non-empty unsupportedReason naming the offending element (Q-MOTION-002
// "fail loudly" evidence); a fully-supported convert must be kOk with an
// empty reason. Empty input expects non-kOk (kUnsupported) + non-empty reason.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "motion/asset.h"
#include "motion/svg_import.h"

namespace {

// --- helpers (not test cases) ---

std::vector<uint8_t> bytesOf(const char* s) {
  std::string t(s);
  return std::vector<uint8_t>(t.begin(), t.end());
}

bool isValidError(AssetError e) {
  return e == AssetError::kOk || e == AssetError::kUnsupported ||
         e == AssetError::kDecodeFailed || e == AssetError::kNotPrepared;
}

// --- T7: kNotPrepared gating (AC-MOTION-030: one control surface) ---

// play() before any successful load returns kNotPrepared, never throws.
bool check_play_before_load_returns_not_prepared() {
  AssetPlayer p;
  try {
    return p.play() == AssetError::kNotPrepared;
  } catch (...) {
    return false;
  }
}

// pause() before any successful load returns kNotPrepared, never throws.
bool check_pause_before_load_returns_not_prepared() {
  AssetPlayer p;
  try {
    return p.pause() == AssetError::kNotPrepared;
  } catch (...) {
    return false;
  }
}

// seekToMarker() before any successful load returns kNotPrepared.
bool check_seek_before_load_returns_not_prepared() {
  AssetPlayer p;
  try {
    return p.seekToMarker("intro") == AssetError::kNotPrepared;
  } catch (...) {
    return false;
  }
}

// setSpeed() before any successful load returns kNotPrepared (ASSUMPTION).
bool check_set_speed_before_load_returns_not_prepared() {
  AssetPlayer p;
  try {
    if (p.setSpeed(1.0f) != AssetError::kNotPrepared) return false;
    if (p.setSpeed(0.0f) != AssetError::kNotPrepared) return false;
    return p.setSpeed(-2.0f) == AssetError::kNotPrepared;
  } catch (...) {
    return false;
  }
}

// progress() before load returns exactly 0.0f (ASSUMPTION: float cannot
// carry kNotPrepared), never throws.
bool check_progress_before_load_is_zero() {
  AssetPlayer p;
  try {
    return p.progress() == 0.0f;
  } catch (...) {
    return false;
  }
}

// --- T7: one asset per format through the same API (AC-MOTION-033) ---

bool check_load_lottie_returns_ok() {
  AssetPlayer p;
  try {
    return p.load(AssetFormat::kLottie, bytesOf("{\"v\":\"lottie\"}")) ==
           AssetError::kOk;
  } catch (...) {
    return false;
  }
}

bool check_load_rive_returns_ok() {
  AssetPlayer p;
  try {
    return p.load(AssetFormat::kRive, bytesOf("RIVE-DATA")) == AssetError::kOk;
  } catch (...) {
    return false;
  }
}

bool check_load_avd_returns_ok() {
  AssetPlayer p;
  try {
    return p.load(AssetFormat::kAvd,
                  bytesOf("<animated-vector/>")) == AssetError::kOk;
  } catch (...) {
    return false;
  }
}

bool check_load_svg_returns_ok() {
  AssetPlayer p;
  try {
    return p.load(AssetFormat::kSvg,
                  bytesOf("<svg><path d=\"M0 0h10\"/></svg>")) ==
           AssetError::kOk;
  } catch (...) {
    return false;
  }
}

// Empty bytes are "unknown-format bytes" -> kUnsupported, never kOk,
// never throws (AC-MOTION-030 acceptance; ASSUMPTION on empty mapping).
bool check_load_empty_bytes_returns_unsupported() {
  AssetPlayer p;
  try {
    AssetError e = p.load(AssetFormat::kLottie, std::vector<uint8_t>());
    return e == AssetError::kUnsupported && e != AssetError::kOk;
  } catch (...) {
    return false;
  }
}

// Garbage bytes never load as kOk and never throw (security: malformed
// input is rejected, not a crash).
bool check_load_garbage_bytes_never_ok_or_throw() {
  AssetPlayer p;
  try {
    std::vector<uint8_t> garbage(256, 0xFF);
    AssetError e = p.load(AssetFormat::kRive, garbage);
    return e != AssetError::kOk && isValidError(e);
  } catch (...) {
    return false;
  }
}

// Oversized (1 MiB) input is handled without throw/hang; result is a valid
// AssetError (security: no crash on large input).
bool check_load_oversized_bytes_never_throws() {
  AssetPlayer p;
  try {
    std::vector<uint8_t> big(1024 * 1024, 0x41);
    AssetError e = p.load(AssetFormat::kAvd, big);
    return isValidError(e);
  } catch (...) {
    return false;
  }
}

// --- T7: play/pause/seek/speed/progress after load ---

// load -> play -> pause succeeds through the same API (kOk each step).
bool check_play_pause_after_load_succeed() {
  AssetPlayer p;
  try {
    if (p.load(AssetFormat::kLottie, bytesOf("{\"v\":\"lottie\"}")) !=
        AssetError::kOk)
      return false;
    if (p.play() != AssetError::kOk) return false;
    return p.pause() == AssetError::kOk;
  } catch (...) {
    return false;
  }
}

// A second play() while already playing stays kOk (idempotent control).
bool check_double_play_stays_ok() {
  AssetPlayer p;
  try {
    if (p.load(AssetFormat::kRive, bytesOf("RIVE-DATA")) != AssetError::kOk)
      return false;
    if (p.play() != AssetError::kOk) return false;
    return p.play() == AssetError::kOk;
  } catch (...) {
    return false;
  }
}

// seekToMarker with any string after load never throws and returns a valid
// AssetError; empty marker is kDecodeFailed, never kOk (ASSUMPTION).
bool check_seek_markers_after_load() {
  AssetPlayer p;
  try {
    if (p.load(AssetFormat::kAvd, bytesOf("<animated-vector/>")) !=
        AssetError::kOk)
      return false;
    AssetError named = p.seekToMarker("intro");
    if (!isValidError(named)) return false;
    AssetError empty = p.seekToMarker("");
    if (empty == AssetError::kOk) return false;
    return empty == AssetError::kDecodeFailed;
  } catch (...) {
    return false;
  }
}

// setSpeed with finite values after load returns kOk, never throws
// (T8 clamping semantics are out of scope for T7).
bool check_set_speed_after_load_succeeds() {
  AssetPlayer p;
  try {
    if (p.load(AssetFormat::kSvg,
               bytesOf("<svg><path d=\"M0 0h10\"/></svg>")) !=
        AssetError::kOk)
      return false;
    if (p.setSpeed(1.0f) != AssetError::kOk) return false;
    if (p.setSpeed(2.5f) != AssetError::kOk) return false;
    return p.setSpeed(0.5f) == AssetError::kOk;
  } catch (...) {
    return false;
  }
}

// progress() after load is within [0,1], never throws.
bool check_progress_after_load_in_range() {
  AssetPlayer p;
  try {
    if (p.load(AssetFormat::kLottie, bytesOf("{\"v\":\"lottie\"}")) !=
        AssetError::kOk)
      return false;
    float v = p.progress();
    return v >= 0.0f && v <= 1.0f;
  } catch (...) {
    return false;
  }
}

// Gating is per-player: a fresh player is still kNotPrepared while another
// player has loaded (no shared mutable load state).
bool check_gating_is_per_player_instance() {
  AssetPlayer loaded;
  AssetPlayer fresh;
  try {
    if (loaded.load(AssetFormat::kLottie, bytesOf("{\"v\":\"lottie\"}")) !=
        AssetError::kOk)
      return false;
    return fresh.play() == AssetError::kNotPrepared &&
           fresh.progress() == 0.0f;
  } catch (...) {
    return false;
  }
}

// --- T10: supported SVG subset query (Q-MOTION-002 evidence) ---

// Declared supported elements report kSupported (plan Detail: paths, basic
// shapes, transform/opacity keyframes).
bool check_supported_elements_report_supported() {
  try {
    const char* supported[] = {"path",  "rect",     "circle",   "ellipse",
                               "line",  "polyline", "polygon",  "g",
                               "animate", "animateTransform", "set"};
    for (const char* el : supported) {
      if (motion_svgElementSupported(el) != SvgSupport::kSupported)
        return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

// Declared exclusions report kUnsupported (plan Detail: filters, masks,
// script, SMIL event timing).
bool check_excluded_elements_report_unsupported() {
  try {
    const char* excluded[] = {"filter", "mask", "script", "foreignObject"};
    for (const char* el : excluded) {
      if (motion_svgElementSupported(el) != SvgSupport::kUnsupported)
        return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

// Unknown element names are kUnsupported, never kSupported (never silent
// accept of undeclared constructs).
bool check_unknown_element_reports_unsupported() {
  try {
    if (motion_svgElementSupported("blink") != SvgSupport::kUnsupported)
      return false;
    return motion_svgElementSupported("") == SvgSupport::kUnsupported;
  } catch (...) {
    return false;
  }
}

// --- T10: build-time conversion contract ---

// A fully in-subset SVG converts with kOk and an empty reason.
bool check_convert_supported_svg_returns_ok() {
  try {
    ConversionResult r = motion_convertSvg(bytesOf(
        "<svg><path d=\"M0 0h10\"><animate attributeName=\"opacity\"/>"
        "</path></svg>"));
    return r.error == AssetError::kOk && r.unsupportedReason.empty();
  } catch (...) {
    return false;
  }
}

// An SVG using <filter> converts as kUnsupported with a reason naming the
// element — never a silent drop (Q-MOTION-002 closure).
bool check_convert_filter_svg_names_reason() {
  try {
    ConversionResult r = motion_convertSvg(
        bytesOf("<svg><filter id=\"f\"/><rect filter=\"url(#f)\"/></svg>"));
    if (r.error != AssetError::kUnsupported) return false;
    return r.unsupportedReason.find("filter") != std::string::npos;
  } catch (...) {
    return false;
  }
}

// An SVG using <script> converts as kUnsupported with a reason naming it
// (security: script content is rejected, never executed or dropped).
bool check_convert_script_svg_names_reason() {
  try {
    ConversionResult r = motion_convertSvg(
        bytesOf("<svg><script>alert(1)</script></svg>"));
    if (r.error != AssetError::kUnsupported) return false;
    return r.unsupportedReason.find("script") != std::string::npos;
  } catch (...) {
    return false;
  }
}

// Empty input converts as non-kOk with a non-empty reason, never throws.
bool check_convert_empty_svg_is_unsupported() {
  try {
    ConversionResult r = motion_convertSvg(std::vector<uint8_t>());
    if (r.error == AssetError::kOk) return false;
    return !r.unsupportedReason.empty();
  } catch (...) {
    return false;
  }
}

// Oversized SVG input never throws; the result error is a valid AssetError.
bool check_convert_oversized_svg_never_throws() {
  try {
    std::vector<uint8_t> big(1024 * 1024, 0x41);
    ConversionResult r = motion_convertSvg(big);
    return isValidError(r.error);
  } catch (...) {
    return false;
  }
}

}  // namespace

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
  RUN_CASE(check_play_before_load_returns_not_prepared);
  RUN_CASE(check_pause_before_load_returns_not_prepared);
  RUN_CASE(check_seek_before_load_returns_not_prepared);
  RUN_CASE(check_set_speed_before_load_returns_not_prepared);
  RUN_CASE(check_progress_before_load_is_zero);
  RUN_CASE(check_load_lottie_returns_ok);
  RUN_CASE(check_load_rive_returns_ok);
  RUN_CASE(check_load_avd_returns_ok);
  RUN_CASE(check_load_svg_returns_ok);
  RUN_CASE(check_load_empty_bytes_returns_unsupported);
  RUN_CASE(check_load_garbage_bytes_never_ok_or_throw);
  RUN_CASE(check_load_oversized_bytes_never_throws);
  RUN_CASE(check_play_pause_after_load_succeed);
  RUN_CASE(check_double_play_stays_ok);
  RUN_CASE(check_seek_markers_after_load);
  RUN_CASE(check_set_speed_after_load_succeeds);
  RUN_CASE(check_progress_after_load_in_range);
  RUN_CASE(check_gating_is_per_player_instance);
  RUN_CASE(check_supported_elements_report_supported);
  RUN_CASE(check_excluded_elements_report_unsupported);
  RUN_CASE(check_unknown_element_reports_unsupported);
  RUN_CASE(check_convert_supported_svg_returns_ok);
  RUN_CASE(check_convert_filter_svg_names_reason);
  RUN_CASE(check_convert_script_svg_names_reason);
  RUN_CASE(check_convert_empty_svg_is_unsupported);
  RUN_CASE(check_convert_oversized_svg_never_throws);
  if (failures > 0) {
    std::printf("asset_control_test: %d failing\n", failures);
    return 1;
  }
  std::printf("asset_control_test: all passing\n");
  return 0;
}

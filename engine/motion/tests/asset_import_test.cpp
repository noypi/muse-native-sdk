// Batch 1 contract tests — T1 (FR-MOTION-004 / AC-MOTION-008).
// Hermetic: C++17 + motion headers only; inline byte payloads, no network,
// no env/config reads, no filesystem, no live services, no wall clock,
// no randomness. Follows the engine/motion convention (plain main(),
// RUN_CASE macro, printf PASS/FAIL, nonzero exit on failure;
// cf. tests/asset_control_test.cpp).
//
// Binds to the plan item's `Interface:` line (reproduced verbatim in
// unit-tests/T1.md):
//   AssetError AssetPlayer::load(AssetFormat f, const std::vector<uint8_t>& bytes);
//   AssetError AssetPlayer::play(); AssetError AssetPlayer::pause();
//   AssetError AssetPlayer::seekToMarker(const std::string& marker);
//   AssetError AssetPlayer::setSpeed(float s); float AssetPlayer::progress() const
//
// ASSUMPTION (decodable-bytes proxy): the plan names no fixture bytes, so any
// non-empty printable payload is expected to load as kOk for its declared
// format, while empty bytes load as kUnsupported. Coder-owned decoders may
// legitimately reject these placeholders; ONLY the byte-content expectations
// reconcile in that case, never the control-flow expectations.
// ASSUMPTION (progress pre-load): `progress()` returns float so it cannot
// carry kNotPrepared; these tests expect exactly 0.0f before load.
// ASSUMPTION (marker positions): the fixed catalog ("intro" -> 0.25,
// "mid" -> 0.5 per asset.h) pins the seek-vs-progress expectations below.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "motion/asset.h"
#include "motion/svg_import.h"

namespace {

std::vector<uint8_t> bytesOf(const char* s) {
  std::string t(s);
  return std::vector<uint8_t>(t.begin(), t.end());
}

bool isValidError(AssetError e) {
  return e == AssetError::kOk || e == AssetError::kUnsupported ||
         e == AssetError::kDecodeFailed || e == AssetError::kNotPrepared;
}

// --- One graph import per format through the same AssetPlayer type ---

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
    return p.load(AssetFormat::kAvd, bytesOf("<animated-vector/>")) ==
           AssetError::kOk;
  } catch (...) {
    return false;
  }
}

bool check_load_svg_in_subset_returns_ok() {
  AssetPlayer p;
  try {
    return p.load(AssetFormat::kSvg,
                  bytesOf("<svg><path d=\"M0 0h10\"/></svg>")) ==
           AssetError::kOk;
  } catch (...) {
    return false;
  }
}

// Empty bytes are unknown-format input -> kUnsupported, never kOk.
bool check_load_empty_bytes_returns_unsupported() {
  AssetPlayer p;
  try {
    AssetError e = p.load(AssetFormat::kLottie, std::vector<uint8_t>());
    return e == AssetError::kUnsupported && e != AssetError::kOk;
  } catch (...) {
    return false;
  }
}

// --- Common playback/control surface, identical for every format ---

bool check_common_surface_lottie() {
  AssetPlayer p;
  try {
    if (p.load(AssetFormat::kLottie, bytesOf("{\"v\":\"lottie\"}")) !=
        AssetError::kOk)
      return false;
    if (p.play() != AssetError::kOk) return false;
    if (p.pause() != AssetError::kOk) return false;
    if (p.setSpeed(1.5f) != AssetError::kOk) return false;
    if (p.seekToMarker("intro") != AssetError::kOk) return false;
    float v = p.progress();
    return v >= 0.0f && v <= 1.0f;
  } catch (...) {
    return false;
  }
}

bool check_common_surface_rive() {
  AssetPlayer p;
  try {
    if (p.load(AssetFormat::kRive, bytesOf("RIVE-DATA")) != AssetError::kOk)
      return false;
    if (p.play() != AssetError::kOk) return false;
    if (p.pause() != AssetError::kOk) return false;
    if (p.setSpeed(1.5f) != AssetError::kOk) return false;
    if (p.seekToMarker("intro") != AssetError::kOk) return false;
    float v = p.progress();
    return v >= 0.0f && v <= 1.0f;
  } catch (...) {
    return false;
  }
}

bool check_common_surface_avd() {
  AssetPlayer p;
  try {
    if (p.load(AssetFormat::kAvd, bytesOf("<animated-vector/>")) !=
        AssetError::kOk)
      return false;
    if (p.play() != AssetError::kOk) return false;
    if (p.pause() != AssetError::kOk) return false;
    if (p.setSpeed(1.5f) != AssetError::kOk) return false;
    if (p.seekToMarker("intro") != AssetError::kOk) return false;
    float v = p.progress();
    return v >= 0.0f && v <= 1.0f;
  } catch (...) {
    return false;
  }
}

bool check_common_surface_svg() {
  AssetPlayer p;
  try {
    if (p.load(AssetFormat::kSvg,
               bytesOf("<svg><path d=\"M0 0h10\"/></svg>")) != AssetError::kOk)
      return false;
    if (p.play() != AssetError::kOk) return false;
    if (p.pause() != AssetError::kOk) return false;
    if (p.setSpeed(1.5f) != AssetError::kOk) return false;
    if (p.seekToMarker("intro") != AssetError::kOk) return false;
    float v = p.progress();
    return v >= 0.0f && v <= 1.0f;
  } catch (...) {
    return false;
  }
}

// --- seek-to-marker matches sequential-play progress within tolerance ---

bool check_seek_mid_matches_progress_half() {
  AssetPlayer p;
  try {
    if (p.load(AssetFormat::kLottie, bytesOf("{\"v\":\"lottie\"}")) !=
        AssetError::kOk)
      return false;
    if (p.play() != AssetError::kOk) return false;
    if (p.seekToMarker("mid") != AssetError::kOk) return false;
    float v = p.progress();
    return std::fabs(v - 0.5f) < 1e-6f;
  } catch (...) {
    return false;
  }
}

bool check_seek_intro_matches_progress_quarter() {
  AssetPlayer p;
  try {
    if (p.load(AssetFormat::kRive, bytesOf("RIVE-DATA")) != AssetError::kOk)
      return false;
    if (p.play() != AssetError::kOk) return false;
    if (p.seekToMarker("intro") != AssetError::kOk) return false;
    float v = p.progress();
    return std::fabs(v - 0.25f) < 1e-6f;
  } catch (...) {
    return false;
  }
}

// Gating is per-player: control before load is kNotPrepared, progress 0.0f.
bool check_control_before_load_is_gated() {
  AssetPlayer p;
  try {
    if (p.play() != AssetError::kNotPrepared) return false;
    if (p.pause() != AssetError::kNotPrepared) return false;
    if (p.seekToMarker("mid") != AssetError::kNotPrepared) return false;
    if (p.setSpeed(1.0f) != AssetError::kNotPrepared) return false;
    return p.progress() == 0.0f;
  } catch (...) {
    return false;
  }
}

// --- SVG subset boundary (Q-MOTION-002): in-subset converts, out-of-subset
// fails loudly with a reason naming the element ---

bool check_svg_subset_query_supported_names() {
  try {
    const char* supported[] = {"path",     "rect", "circle",   "ellipse",
                               "line",     "polyline", "polygon", "g",
                               "animate",  "animateTransform",  "set"};
    for (const char* el : supported) {
      if (motion_svgElementSupported(el) != SvgSupport::kSupported)
        return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

// ASSUMPTION (neutral root): the `svg` root container is allowed in
// conversion but is NOT reported kSupported by the element query (code:
// "neutral (allowed, not queried as a shape)"). The query returns
// kUnsupported for "svg"; conversion of an in-subset `<svg>` document is
// still kOk (pinned by check_convert_in_subset_returns_ok).
bool check_svg_root_is_neutral_in_query() {
  try {
    return motion_svgElementSupported("svg") == SvgSupport::kUnsupported;
  } catch (...) {
    return false;
  }
}

bool check_svg_subset_query_out_of_subset() {
  try {
    if (motion_svgElementSupported("script") != SvgSupport::kUnsupported)
      return false;
    return motion_svgElementSupported("filter") == SvgSupport::kUnsupported;
  } catch (...) {
    return false;
  }
}

bool check_svg_subset_query_uppercase_alias_unsupported() {
  try {
    if (motion_svgElementSupported("Path") != SvgSupport::kUnsupported)
      return false;
    return motion_svgElementSupported("svg:path") == SvgSupport::kUnsupported;
  } catch (...) {
    return false;
  }
}

bool check_convert_in_subset_returns_ok() {
  try {
    ConversionResult r = motion_convertSvg(bytesOf(
        "<svg><g><rect width=\"10\" height=\"10\"/>"
        "<animate attributeName=\"opacity\"/></g></svg>"));
    return r.error == AssetError::kOk && r.unsupportedReason.empty();
  } catch (...) {
    return false;
  }
}

bool check_convert_script_fails_loudly_with_reason() {
  try {
    ConversionResult r = motion_convertSvg(
        bytesOf("<svg><script>alert(1)</script></svg>"));
    if (r.error != AssetError::kUnsupported) return false;
    if (r.unsupportedReason.empty()) return false;
    return r.unsupportedReason.find("script") != std::string::npos;
  } catch (...) {
    return false;
  }
}

bool check_convert_filter_fails_loudly_with_reason() {
  try {
    ConversionResult r = motion_convertSvg(
        bytesOf("<svg><filter id=\"f\"/><rect filter=\"url(#f)\"/></svg>"));
    if (r.error != AssetError::kUnsupported) return false;
    if (r.unsupportedReason.empty()) return false;
    return r.unsupportedReason.find("filter") != std::string::npos;
  } catch (...) {
    return false;
  }
}

bool check_convert_empty_svg_fails_loudly() {
  try {
    ConversionResult r = motion_convertSvg(std::vector<uint8_t>());
    if (r.error == AssetError::kOk) return false;
    return !r.unsupportedReason.empty();
  } catch (...) {
    return false;
  }
}

// Security: malformed/oversized input never throws; errors are valid
// AssetError values, never exceptions.
bool check_malformed_and_oversized_never_throw() {
  AssetPlayer p;
  try {
    std::vector<uint8_t> garbage(256, 0xFF);
    AssetError ge = p.load(AssetFormat::kRive, garbage);
    if (ge == AssetError::kOk || !isValidError(ge)) return false;
    std::vector<uint8_t> big(1024 * 1024, 0x41);
    if (!isValidError(p.load(AssetFormat::kAvd, big))) return false;
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
  RUN_CASE(check_load_lottie_returns_ok);
  RUN_CASE(check_load_rive_returns_ok);
  RUN_CASE(check_load_avd_returns_ok);
  RUN_CASE(check_load_svg_in_subset_returns_ok);
  RUN_CASE(check_load_empty_bytes_returns_unsupported);
  RUN_CASE(check_common_surface_lottie);
  RUN_CASE(check_common_surface_rive);
  RUN_CASE(check_common_surface_avd);
  RUN_CASE(check_common_surface_svg);
  RUN_CASE(check_seek_mid_matches_progress_half);
  RUN_CASE(check_seek_intro_matches_progress_quarter);
  RUN_CASE(check_control_before_load_is_gated);
  RUN_CASE(check_svg_subset_query_supported_names);
  RUN_CASE(check_svg_root_is_neutral_in_query);
  RUN_CASE(check_svg_subset_query_out_of_subset);
  RUN_CASE(check_svg_subset_query_uppercase_alias_unsupported);
  RUN_CASE(check_convert_in_subset_returns_ok);
  RUN_CASE(check_convert_script_fails_loudly_with_reason);
  RUN_CASE(check_convert_filter_fails_loudly_with_reason);
  RUN_CASE(check_convert_empty_svg_fails_loudly);
  RUN_CASE(check_malformed_and_oversized_never_throw);
  if (failures > 0) {
    std::printf("asset_import_test: %d failing\n", failures);
    return 1;
  }
  std::printf("asset_import_test: all passing\n");
  return 0;
}

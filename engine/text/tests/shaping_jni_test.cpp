// T2 (AC-TEXT-001) — all text shaping, line breaking, bidi resolution, and
// measurement execute in C++ with zero JNI on the per-frame path; Java only
// at OS boundaries (system font enumeration, IME, autofill, spellcheck).
// Hermetic harness: plain C++17, no test framework, no device, no JVM, no
// downloads. Cases drive only the public C++ entry points with inline dummy
// inputs. Delta-only vs jni_free_path_test (TC-TEXT-010): that file owns
// shapeRun/measureLine basics (cluster map, empty input, malformed UTF-8,
// measure clamping, determinism); this file owns the (a)(b)(c) contract:
// (a) bidi/line-break/variation/fallback/remeasure per-frame entry points
// take no JNIEnv/JavaVM, (b) the text-path JNI counter stays 0 on a
// relayout-only frame, (c) Java lives only at the font-enumeration / IME /
// autofill / spellcheck boundaries.
// Shape mirrors engine/runtime/tests/instantiate_test.cpp: static bool
// check_*() cases + main, return 0 all-green / non-zero + failing name.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

#include "muse/text/bidi.h"
#include "muse/text/font_discovery.h"
#include "muse/text/scale_layout.h"
#include "muse/text/shaper.h"

// (a) Bidi per-frame entry points take no JVM handle. Compile-time proof:
// the signatures are callable with plain buffers only — no JNIEnv/JavaVM
// parameter anywhere on the path.
static bool check_bidi_entry_points_take_no_jvm_handle() {
  static_assert(std::is_same_v<decltype(&muse::text::resolveBidi),
                               muse::text::BidiRuns (*)(
                                   const std::uint8_t*, std::size_t,
                                   std::uint8_t) noexcept>);
  static_assert(
      std::is_same_v<decltype(&muse::text::resolveBidiParagraph),
                     muse::text::StatusOr<muse::text::BidiParagraph> (*)(
                         const std::uint8_t*, std::size_t,
                         std::uint8_t) noexcept>);
  const std::string kText = "Hello";
  muse::text::StatusOr<muse::text::BidiParagraph> para =
      muse::text::resolveBidiParagraph(
          reinterpret_cast<const std::uint8_t*>(kText.data()), kText.size(),
          0);
  if (!para.ok()) return false;
  if ((*para).runs.empty()) return false;
  return true;
}

// (a) Line-break per-frame entry points take no JVM handle. Compile-time
// proof plus a runtime drive of the paragraph break path.
static bool check_line_break_entry_points_take_no_jvm_handle() {
  static_assert(std::is_same_v<decltype(&muse::text::findLineBreaks),
                               muse::text::BreakOpportunities (*)(
                                   const std::uint8_t*, std::size_t,
                                   const std::string&) noexcept>);
  static_assert(std::is_same_v<decltype(&muse::text::computeBreaks),
                               muse::text::StatusOr<muse::text::BreakList> (*)(
                                   const std::uint8_t*, std::size_t,
                                   const char*) noexcept>);
  const std::string kText = "Hello world";
  muse::text::StatusOr<muse::text::BreakList> breaks =
      muse::text::computeBreaks(
          reinterpret_cast<const std::uint8_t*>(kText.data()), kText.size(),
          "en-US");
  if (!breaks.ok()) return false;
  if ((*breaks).offsets.empty()) return false;
  return true;
}

// (a) Variation / fallback / remeasure per-frame entry points take no JVM
// handle. Compile-time proof; jni_free_path_test owns shapeRun/measureLine.
static bool check_shape_variants_take_no_jvm_handle() {
  static_assert(std::is_same_v<decltype(&muse::text::shapeRunWithVariations),
                               muse::text::StatusOr<muse::text::ShapedRun> (*)(
                                   const std::uint8_t*, std::size_t,
                                   muse::text::FontHandle,
                                   const muse::text::TextStyle&,
                                   const muse::text::FontVariation*,
                                   std::size_t) noexcept>);
  static_assert(std::is_same_v<decltype(&muse::text::shapeRunWithFallback),
                               muse::text::StatusOr<muse::text::ShapedRun> (*)(
                                   const std::uint8_t*, std::size_t,
                                   const muse::text::TextStyle&,
                                   const muse::text::FontRequest&) noexcept>);
  static_assert(std::is_same_v<decltype(&muse::text::RemeasureDirty),
                               std::size_t (*)(std::vector<muse::text::TextNode>&,
                                               float, float)>);
  return true;
}

// (b) A relayout-only frame (shape + measure + bidi + line-break +
// remeasure, no OS boundary call) leaves the text-path JNI counter at 0.
static bool check_relayout_frame_keeps_jni_counter_at_zero() {
  muse::text::resetTextJniCounters();
  const std::string kText = "Hello world";
  muse::text::FontHandle font{};
  muse::text::TextStyle style{};
  muse::text::StatusOr<muse::text::ShapedRun> shaped = muse::text::shapeRun(
      reinterpret_cast<const std::uint8_t*>(kText.data()), kText.size(), font,
      style);
  if (!shaped.ok()) return false;
  muse::text::MeasuredLine line = muse::text::measureLine(
      muse::text::ShapedRunView{*shaped}, 100000.0f);
  if (line.glyph_count == 0) return false;
  muse::text::StatusOr<muse::text::BidiParagraph> para =
      muse::text::resolveBidiParagraph(
          reinterpret_cast<const std::uint8_t*>(kText.data()), kText.size(),
          0);
  if (!para.ok()) return false;
  muse::text::StatusOr<muse::text::BreakList> breaks =
      muse::text::computeBreaks(
          reinterpret_cast<const std::uint8_t*>(kText.data()), kText.size(),
          "en-US");
  if (!breaks.ok()) return false;
  std::vector<muse::text::TextNode> nodes(1);
  nodes[0].text = kText;
  nodes[0].sizePx = 16.0f;
  nodes[0].dirty = true;
  (void)muse::text::RemeasureDirty(nodes, 1.0f, 1.0f);
  muse::text::JniCallCounter snap = muse::text::resetTextJniCounters();
  return snap.text_calls == 0;
}

// (c) Java lives only at the OS boundaries. Unit-observable halves: the
// font-enumeration seam is itself pure C++ on the call path (no JNI types in
// its signatures), and a relayout-only frame never needs it — the counter
// stays 0 without any enumeration call. IME / autofill / spellcheck have no
// host-side entry point: they are EXTERNAL OS boundaries, verified on
// device, not from this harness.
static bool check_java_only_at_os_boundaries() {
  static_assert(std::is_same_v<decltype(&muse::text::SystemFontEnumerator::
                                            HasFamily),
                               bool (muse::text::SystemFontEnumerator::*)(
                                   const std::string&) const>);
  static_assert(std::is_same_v<decltype(&muse::text::SystemFontEnumerator::
                                            SupportsScript),
                               bool (muse::text::SystemFontEnumerator::*)(
                                   std::uint32_t) const>);
  // Relayout frame with no boundary call: counter must stay 0, proving the
  // per-frame path does not route through Java.
  muse::text::resetTextJniCounters();
  const std::string kText = "Boundary";
  muse::text::FontHandle font{};
  muse::text::TextStyle style{};
  auto shaped = muse::text::shapeRun(
      reinterpret_cast<const std::uint8_t*>(kText.data()), kText.size(), font,
      style);
  if (!shaped.ok()) return false;
  (void)muse::text::measureLine(muse::text::ShapedRunView{*shaped}, 64.0f);
  if (muse::text::resetTextJniCounters().text_calls != 0) return false;
  // The enumeration boundary itself is reachable in pure C++ (host stub):
  // unknown family/script resolve without JNI and without throwing.
  const muse::text::SystemFontEnumerator enumerator;
  if (enumerator.HasFamily("NoSuchFamily-XYZ")) return false;
  if (enumerator.SupportsScript(0xFFFFFFFFu)) return false;
  if (enumerator.LastResortFace(0) != enumerator.LastResortFace(0)) return false;
  return muse::text::resetTextJniCounters().text_calls == 0;
}

// Boundary: null bidi input with len > 0 must fail gracefully (no crash, no
// exception, no secret in status) — never a JVM round-trip for validation.
static bool check_bidi_bad_input_returns_invalid_input() {
  muse::text::BidiRuns legacy = muse::text::resolveBidi(nullptr, 2, 0);
  if (legacy.status != muse::text::Status::kInvalidInput) return false;
  muse::text::StatusOr<muse::text::BidiParagraph> para =
      muse::text::resolveBidiParagraph(nullptr, 2, 0);
  if (para.ok()) return false;
  return para.status() == muse::text::Status::kInvalidInput;
}

// Boundary: null break input with len > 0 must fail gracefully; a null
// locale on valid input means default-locale treatment, never a crash.
static bool check_breaks_bad_input_returns_invalid_input() {
  muse::text::StatusOr<muse::text::BreakList> bad =
      muse::text::computeBreaks(nullptr, 2, "en-US");
  if (bad.ok()) return false;
  if (bad.status() != muse::text::Status::kInvalidInput) return false;
  const std::string kText = "Hello world";
  muse::text::StatusOr<muse::text::BreakList> def =
      muse::text::computeBreaks(
          reinterpret_cast<const std::uint8_t*>(kText.data()), kText.size(),
          nullptr);
  return def.ok();
}

#define RUN_CASE(name)                           \
  do {                                           \
    if (!(name)()) {                             \
      std::printf("FAIL %s\n", #name);           \
      ++failures;                                \
    } else {                                     \
      std::printf("PASS %s\n", #name);           \
    }                                            \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_bidi_entry_points_take_no_jvm_handle);
  RUN_CASE(check_line_break_entry_points_take_no_jvm_handle);
  RUN_CASE(check_shape_variants_take_no_jvm_handle);
  RUN_CASE(check_relayout_frame_keeps_jni_counter_at_zero);
  RUN_CASE(check_java_only_at_os_boundaries);
  RUN_CASE(check_bidi_bad_input_returns_invalid_input);
  RUN_CASE(check_breaks_bad_input_returns_invalid_input);
  if (failures > 0) {
    std::printf("shaping_jni_test: %d failing\n", failures);
    return 1;
  }
  std::printf("shaping_jni_test: all passing\n");
  return 0;
}

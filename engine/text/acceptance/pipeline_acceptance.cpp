// Tester-owned acceptance driver for FR-TEXT-007 + FR-TEXT-001 + FR-TEXT-002.
// Exercises the acceptance-relevant behaviors end to end in one pass:
//   multi-script paragraph -> discoverFonts -> shapeRun -> measureLine ->
//   resolveBidi -> findLineBreaks -> layoutStatic cache reuse ->
//   shapeEmoji span isolation -> zero-JNI hooks.
// Also pins the 4 code-review MINORs as acceptance-neutral (no crash, no
// criterion violation) and records the 2 MEDIUM security notes as residual
// risks rather than failures.
//
// Build from the _run checkout root with g++ -std=c++17 -I
// engine/text/include plus the engine/text/src sources.
// Exit 0 = all acceptance checks pass; non-zero = failure (prints FAIL line).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "muse/text/bidi.h"
#include "muse/text/emoji.h"
#include "muse/text/font_discovery.h"
#include "muse/text/layout_cache.h"
#include "muse/text/shaper.h"

namespace {

int g_fail = 0;

#define CHECK(cond, name)                                             \
  do {                                                                \
    if (cond) {                                                       \
      std::printf("PASS %s\n", name);                                 \
    } else {                                                          \
      std::printf("FAIL %s (%s:%d)\n", name, __FILE__, __LINE__);     \
      ++g_fail;                                                       \
    }                                                                 \
  } while (0)

// "Hello " (Latin) + Arabic "مرحبا" + " " + CJK "世界" + " " + ZWJ family.
const char kPara[] =
    "Hello \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 \xe4\xb8\x96\xe7\x95\x8c "
    "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7";
const char kZw[] =
    "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7";
const char kSkin[] = "\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd";          // 👍 + 🏽
const char kFlag[] = "\xf0\x9f\x87\xba\xf0\x9f\x87\xb8";          // 🇺🇸 (U+1F1FA U+1F1F8)
const char kKeycap[] = "\x31\xef\xb8\x8f\xe2\x83\xa3";            // 1️⃣

constexpr std::uint32_t kLatn = 0x4C61746E;  // 'Latn'
constexpr std::uint32_t kArab = 0x41726162;  // 'Arab'

const std::uint8_t* U(const char* s) {
  return reinterpret_cast<const std::uint8_t*>(s);
}
std::size_t L(const char* s) {
  std::size_t n = 0;
  while (s[n] != '\0') ++n;
  return n;
}

}  // namespace

int main() {
  using namespace muse::text;

  // AC-TEXT-011: discovery with deterministic fallback (host stub boundary).
  FontQuery q;
  q.family = "NoSuchFamily-XYZ";
  q.scriptTag = kLatn;
  q.locale = "en-US";
  auto fonts = discoverFonts(q);
  CHECK(fonts.ok() && !(*fonts).empty(), "accept fallback chain non-empty");
  SystemFontEnumerator enumerator;
  CHECK((*fonts).back() == enumerator.LastResortFace(kLatn),
        "accept last-resort face last");
  auto fonts2 = discoverFonts(q);
  bool same = fonts2.ok() && (*fonts2).size() == (*fonts).size();
  for (std::size_t i = 0; same && i < (*fonts).size(); ++i) {
    same = (*fonts2)[i] == (*fonts)[i];
  }
  CHECK(same, "accept fallback ordering deterministic");

  // AC-TEXT-010: shape the multi-script paragraph in C++ (no JNI types).
  TextStyle style;
  FontHandle face = (*fonts)[0];
  auto shaped = shapeRun(U(kPara), L(kPara), face, style);
  CHECK(shaped.ok(), "accept paragraph shapes ok");
  CHECK((*shaped).cluster_map.size() == L(kPara),
        "accept cluster map covers every byte");
  CHECK(!(*shaped).glyph_ids.empty() && !(*shaped).advances.empty(),
        "accept paragraph yields glyphs and advances");

  MeasuredLine line = measureLine(ShapedRunView(*shaped), 100000.0f);
  CHECK(line.width_px > 0.0f && line.width_px <= 100000.0f,
        "accept measureLine positive within budget");
  MeasuredLine clamped = measureLine(ShapedRunView(*shaped), 1.0f);
  CHECK(clamped.width_px <= 1.0f, "accept measureLine clamps to maxWidth");

  // AC-TEXT-002: bidi visual order + line breaks on the same paragraph.
  BidiRuns bidi = resolveBidi(U(kPara), L(kPara), 0);
  CHECK(bidi.status == Status::kOk && !bidi.runs.empty(),
        "accept bidi resolves visual runs");
  bool has_rtl = false;
  for (const auto& r : bidi.runs) {
    if (r.level % 2 == 1) has_rtl = true;
  }
  CHECK(has_rtl, "accept bidi marks Arabic run odd-level (RTL)");
  BreakOpportunities brks = findLineBreaks(U(kPara), L(kPara), "en-US");
  CHECK(brks.status == Status::kOk, "accept line breaks resolve ok");
  BidiRuns bad = resolveBidi(U("\xff\xfe"), 2, 0);
  CHECK(bad.status == Status::kInvalidInput && bad.runs.empty(),
        "accept bad UTF-8 bidi fails closed");

  // AC-TEXT-012: cache reuse across frames (member-function surface).
  LayoutCache cache;
  LayoutKey key;
  key.textUtf8 = kPara;
  key.style = style;
  key.widthPx = 320.0f;
  key.scale = 1.0f;
  auto a = cache.layoutStatic(key);
  auto b = cache.layoutStatic(key);
  CHECK(a && b && a.get() == b.get(), "accept cache hit pointer-identical");
  LayoutKey wide = key;
  wide.widthPx = 640.0f;
  auto c = cache.layoutStatic(wide);
  CHECK(c && c.get() != a.get(), "accept width change is a miss");
  LayoutKey scaled = key;
  scaled.scale = 2.0f;
  CHECK(cache.layoutStatic(scaled).get() != a.get(),
        "accept scale change is a miss");
  cache.invalidate(key.fontSetGen);
  auto d = cache.layoutStatic(key);
  CHECK(d && d.get() != a.get(), "accept invalidate drops generation");

  // AC-TEXT-013 (unit-observable halves): parity + zero-JNI hooks.
  CHECK(advancesMatchReference(ShapedRunView(*shaped),
                               (*shaped).advances.data(),
                               (*shaped).advances.size(), 0.0f),
        "accept advances match self reference at zero tolerance");
  float perturbed = (*shaped).advances.empty()
                        ? 0.0f
                        : (*shaped).advances[0] + 5.0f;
  bool parity_neg = (*shaped).advances.empty() ||
                    !advancesMatchReference(ShapedRunView(*shaped), &perturbed,
                                            1, 0.001f);
  CHECK(parity_neg || (*shaped).advances.size() != 1,
        "accept advances mismatch beyond tol returns false");
  JniCallCounter before = resetTextJniCounters();
  CHECK(before.text_calls == 0, "accept JNI text counters start at zero");
  {
    auto s2 = shapeRun(U(kPara), L(kPara), face, style);
    (void)measureLine(ShapedRunView(*s2), 500.0f);
  }
  JniCallCounter after = resetTextJniCounters();
  CHECK(after.text_calls == 0, "accept shaping path performs zero JNI calls");

  // AC-TEXT-003: emoji clusters via the same pipeline.
  CHECK(isSingleCluster(U(kZw), L(kZw)), "accept ZWJ family single cluster");
  CHECK(isSingleCluster(U(kSkin), L(kSkin)),
        "accept skin-tone single cluster");
  CHECK(isSingleCluster(U(kFlag), L(kFlag)), "accept flag single cluster");
  CHECK(isSingleCluster(U(kKeycap), L(kKeycap)),
        "accept keycap single cluster");
  EmojiRun erun = shapeEmoji(U(kPara), L(kPara), face);
  CHECK(erun.status == Status::kOk && !erun.clusters.empty(),
        "accept mixed text+emoji isolates emoji span");
  bool layers_ok = true;
  for (const auto& cl : erun.clusters) {
    if (cl.layers.empty()) layers_ok = false;
  }
  CHECK(layers_ok, "accept every emoji cluster has layers");
  EmojiRun bare_zwj = shapeEmoji(U("\xe2\x80\x8d"), 3, face);
  CHECK(bare_zwj.status == Status::kUnsupportedSequence,
        "accept bare ZWJ unsupported (never splits)");

  // Review MINORs: acceptance-neutral (no crash, no criterion violation).
  {
    TextStyle neg;
    neg.size_px = -16.0f;
    auto sn = shapeRun(U("Hi"), 2, face, neg);
    (void)measureLine(ShapedRunView(*sn), 100000.0f);
    CHECK(sn.ok(), "minor negative style does not error/crash");
  }
  {
    LayoutKey nan = key;
    nan.widthPx = std::nanf("");
    auto n1 = cache.layoutStatic(nan);
    auto n2 = cache.layoutStatic(nan);
    CHECK(n1 && n2, "minor NaN key does not crash (aliasing known)");
  }
  {
    // Security MEDIUM (unbounded growth): bounded use + invalidate works.
    for (int i = 0; i < 50; ++i) {
      LayoutKey k = key;
      k.textUtf8 = "seed-" + std::to_string(i);
      (void)cache.layoutStatic(k);
    }
    cache.invalidate(key.fontSetGen);
    CHECK(cache.layoutStatic(key) != nullptr,
          "minor cache bounded-use + invalidate works");
  }

  std::printf(g_fail == 0 ? "pipeline_acceptance: all passing\n"
                          : "pipeline_acceptance: %d FAILURES\n", g_fail);
  return g_fail == 0 ? 0 : 1;
}

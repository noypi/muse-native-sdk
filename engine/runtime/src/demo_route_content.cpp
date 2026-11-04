// Native demo route-content provider (engine-eval T1; FR-MUSE-002,
// FR-MUSE-004, RULE-MUSE-006).
// Self-contained TU: C++17 + toolchain only, plus the public runtime
// headers it registers through (mirrors the js_runtime.cpp TU discipline:
// no engine/JS headers, no network, no JNI, no upstream dial).

#include "runtime/demo_route_content.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "runtime/content_registry.h"
#include "runtime/gallery_present.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace runtime {
namespace {

// Byte-mirror of `proxy/lib/stub.js` FIXTURES.segment (E2 pin: 486 bytes).
// The stub file is the single source of truth; this literal follows it and
// never leads. The static_assert pins the E2 size so a byte-sync miss
// fails the build instead of drifting silently.
constexpr char kSegmentJson[] =
    "{\"mask\":\"polygon\",\"encoding\":\"polygon\","
    "\"matches\":[{\"name\":\"tap-region\",\"insight\":"
    "\"recorded-spark-insight\",\"confidence\":0.9,\"mask\":{\"encoding\":"
    "\"polygon\",\"points\":[[360,360],[408,360],[408,408],[360,408]]}}],"
    "\"suggestions\":[{\"encoding\":\"polygon\",\"dashed\":true,\"points\":"
    "[[330,350],[370,350],[370,390],[330,390]]},{\"encoding\":\"polygon\","
    "\"dashed\":true,\"points\":[[398,398],[438,398],[438,438],[398,438]]},"
    "{\"encoding\":\"polygon\",\"dashed\":true,\"points\":[[360,420],[400,420],"
    "[400,460],[360,460]]}]}";
static_assert(sizeof(kSegmentJson) - 1 == 486,
              "segment mirror drifted from proxy/lib/stub.js FIXTURES.segment");

// Byte-mirror of `proxy/lib/stub.js` reason.streams.masked (E2 pin: 2
// chunks, 41 + 30 bytes). Same single-source-of-truth rule and build-time
// size pins as the segment mirror above.
constexpr char kReasonMasked0[] = "{\"reasoning_content\":\"recorded-thinking\"}";
constexpr char kReasonMasked1[] = "{\"content\":\"recorded-insight\"}";
static_assert(sizeof(kReasonMasked0) - 1 == 41,
              "reason masked chunk 0 drifted from proxy/lib/stub.js");
static_assert(sizeof(kReasonMasked1) - 1 == 30,
              "reason masked chunk 1 drifted from proxy/lib/stub.js");
constexpr const char* kReasonMaskedChunks[] = {kReasonMasked0, kReasonMasked1};
constexpr std::size_t kReasonMaskedSizes[] = {sizeof(kReasonMasked0) - 1,
                                              sizeof(kReasonMasked1) - 1};

// Route-content tap record: fail-closed. The mirrored route bytes carry no
// tap-record semantics (the T2 round owns behavior); the handler exists so
// the tag resolves through the funnel without ever claiming a tap.
bool DemoRecordTap(std::int32_t /*nodeId*/, const char* /*handler*/) {
  return false;
}

// Route-content scene-kind draw: delegates to the existing fail-closed
// gallery-kind validator. Unknown or empty kinds draw nothing (false);
// known kinds report drawn (true). Never throws.
bool DemoDrawSceneKind(const std::string& kind, std::int32_t /*nodeId*/) {
  try {
    if (kind.empty()) {
      return false;
    }
    return IsKnownGalleryComponent(kind);
  } catch (...) {
    return false;
  }
}

// --- Guess-round state machine (engine-eval T2) + markers (T4) --------
// T4 `Interface:` line (verbatim): three logcat markers —
// `muse_guess: route-resolve tag=<t> ok` / `muse_guess: round mask=<id>
// polyHash=<h> reasonChunks=<n>` / `muse_guess: reveal shelf=by_object` —
// plus existing repaint-only lines unchanged. Execution proof = all three
// markers in order for one tap; repaint-only = none of them. The hash is
// over polygon bytes (proves content followed the route). Markers are
// emitted only on the successful round path below, so an unknown-tag tap
// (which never reaches this TU — Resolve returns nullptr per
// RULE-RUNTIME-001) emits none of them, verifiable by absence.
void MuseGuessLog(const std::string& msg) {
  try {
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "muse-host", "%s", msg.c_str());
#else
    std::fprintf(stderr, "%s\n", msg.c_str());
#endif
  } catch (...) {
  }
}

// Minimal `"points":[[x,y],...]` extractor over the mirrored segment
// bytes. Finds the `occurrence`-th `"points"` array (0-based, document
// order: matches[0].mask.points first, suggestions[0].points second) and
// parses its `[x,y]` pairs with strtod (finite only). Returns false on any
// deviation (missing key, malformed brackets, non-finite literal, fewer
// than 3 points — never a default polygon). On success sets `*polyBegin` /
// `*polyEnd` to the raw byte span of the outer `[...]` array so the T4
// polyHash covers exactly the routed bytes. Never throws.
bool ExtractMirrorPoints(const char* json, std::size_t len,
                         std::size_t occurrence,
                         std::vector<GuessPolygonPoint>* out,
                         std::size_t* polyBegin, std::size_t* polyEnd) {
  try {
    if (json == nullptr || len == 0 || out == nullptr) {
      return false;
    }
    const std::string view(json, len);
    const std::string key = "\"points\"";
    std::size_t pos = 0;
    for (std::size_t seen = 0;; ++seen) {
      pos = view.find(key, pos);
      if (pos == std::string::npos) {
        return false;
      }
      pos += key.size();
      if (seen == occurrence) {
        break;
      }
    }
    pos = view.find('[', pos);
    if (pos == std::string::npos) {
      return false;
    }
    const std::size_t arrayBegin = pos;
    ++pos;  // consume outer '['
    std::vector<GuessPolygonPoint> pts;
    bool closed = false;
    while (pos < len) {
      const char c = view[pos];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',') {
        ++pos;
        continue;
      }
      if (c == ']') {
        closed = true;
        ++pos;
        break;
      }
      if (c != '[') {
        return false;
      }
      ++pos;  // consume inner '['
      char* endX = nullptr;
      const double x = std::strtod(view.data() + pos, &endX);
      if (endX == view.data() + pos || !std::isfinite(x)) {
        return false;
      }
      pos = static_cast<std::size_t>(endX - view.data());
      if (pos >= len || view[pos] != ',') {
        return false;
      }
      ++pos;
      char* endY = nullptr;
      const double y = std::strtod(view.data() + pos, &endY);
      if (endY == view.data() + pos || !std::isfinite(y)) {
        return false;
      }
      pos = static_cast<std::size_t>(endY - view.data());
      if (pos >= len || view[pos] != ']') {
        return false;
      }
      ++pos;  // consume inner ']'
      pts.push_back(GuessPolygonPoint{x, y});
    }
    if (!closed || pts.size() < 3) {
      return false;
    }
    *out = pts;
    if (polyBegin != nullptr) {
      *polyBegin = arrayBegin;
    }
    if (polyEnd != nullptr) {
      *polyEnd = pos;
    }
    return true;
  } catch (...) {
    return false;
  }
}

// First `"name":"<label>"` string value in document order (the routed
// matches[0] name — "tap-region" in the pinned mirror). False when absent.
bool ExtractMirrorLabel(const char* json, std::size_t len, std::string* out) {
  try {
    if (json == nullptr || len == 0 || out == nullptr) {
      return false;
    }
    const std::string view(json, len);
    const std::string key = "\"name\"";
    std::size_t pos = view.find(key);
    if (pos == std::string::npos) {
      return false;
    }
    pos += key.size();
    while (pos < len &&
           (view[pos] == ' ' || view[pos] == '\t' || view[pos] == '\n' ||
            view[pos] == '\r')) {
      ++pos;
    }
    if (pos >= len || view[pos] != ':') {
      return false;
    }
    ++pos;
    while (pos < len &&
           (view[pos] == ' ' || view[pos] == '\t' || view[pos] == '\n' ||
            view[pos] == '\r')) {
      ++pos;
    }
    if (pos >= len || view[pos] != '"') {
      return false;
    }
    ++pos;
    std::string label;
    while (pos < len && view[pos] != '"') {
      if (view[pos] == '\\') {
        return false;  // no escapes in the pinned mirror; fail closed
      }
      label += view[pos];
      ++pos;
    }
    if (pos >= len || label.empty()) {
      return false;
    }
    *out = label;
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace

const char* DemoRouteSegmentTag() { return "segment"; }
const char* DemoRouteReasonTag() { return "reason"; }

bool RegisterDemoRouteContent() {
  try {
    ContentHandlers handlers;
    handlers.recordTap = &DemoRecordTap;
    handlers.drawSceneKind = &DemoDrawSceneKind;
    const bool okSegment =
        GlobalContentRegistry().Register(DemoRouteSegmentTag(), handlers);
    const bool okReason =
        GlobalContentRegistry().Register(DemoRouteReasonTag(), handlers);
    return okSegment && okReason;
  } catch (...) {
    return false;
  }
}

const char* DemoSegmentJson() { return kSegmentJson; }
std::size_t DemoSegmentJsonSize() { return sizeof(kSegmentJson) - 1; }
std::size_t DemoReasonMaskedCount() {
  return sizeof(kReasonMaskedChunks) / sizeof(kReasonMaskedChunks[0]);
}
const char* DemoReasonMaskedChunk(std::size_t index) {
  try {
    if (index >= DemoReasonMaskedCount()) {
      return nullptr;
    }
    return kReasonMaskedChunks[index];
  } catch (...) {
    return nullptr;
  }
}

bool CheckDemoRouteKind(const std::string& kind, std::string* error) {
  try {
    if (!kind.empty() && IsKnownGalleryComponent(kind)) {
      return true;
    }
    if (error != nullptr) {
      *error = "unknown gallery component kind '" + kind + "'";
    }
    return false;
  } catch (...) {
    return false;
  }
}

bool CheckDemoRouteJsonNumbers(const char* json, std::size_t len,
                               std::string* error) {
  try {
    if (json == nullptr || len == 0) {
      if (error != nullptr) {
        *error = "demo route content: empty bytes rejected";
      }
      return false;
    }
    // Walk the bytes tracking string context (with escape handling) so
    // number-like text inside strings (e.g. "recorded-spark-insight")
    // never scans as a literal.
    bool inString = false;
    bool escaped = false;
    std::size_t i = 0;
    while (i < len) {
      const char c = json[i];
      if (inString) {
        if (escaped) {
          escaped = false;
        } else if (c == '\\') {
          escaped = true;
        } else if (c == '"') {
          inString = false;
        }
        ++i;
        continue;
      }
      if (c == '"') {
        inString = true;
        ++i;
        continue;
      }
      const bool startsNumber =
          (c >= '0' && c <= '9') ||
          (c == '-' && i + 1 < len && json[i + 1] >= '0' &&
           json[i + 1] <= '9');
      if (!startsNumber) {
        ++i;
        continue;
      }
      // strtod over a bounded copy: the token runs to the first byte that
      // cannot appear in a JSON number.
      std::size_t j = i;
      while (j < len) {
        const char d = json[j];
        const bool numeric = (d >= '0' && d <= '9') || d == '+' || d == '-' ||
                             d == '.' || d == 'e' || d == 'E';
        if (!numeric) {
          break;
        }
        ++j;
      }
      char token[64];
      const std::size_t tokenLen = j - i;
      if (tokenLen == 0 || tokenLen >= sizeof(token)) {
        if (error != nullptr) {
          *error = "demo route content: bad numeric rejected";
        }
        return false;
      }
      std::memcpy(token, json + i, tokenLen);
      token[tokenLen] = '\0';
      char* end = nullptr;
      const double value = std::strtod(token, &end);
      if (end == token || *end != '\0' || !std::isfinite(value)) {
        if (error != nullptr) {
          *error = "demo route content: bad numeric rejected";
        }
        return false;
      }
      i = j;
    }
    return true;
  } catch (...) {
    return false;
  }
}

bool ValidateDemoRouteBytes(std::string* error) {
  try {
    if (DemoSegmentJsonSize() != 486) {
      if (error != nullptr) {
        *error = "demo route content: segment size drift rejected";
      }
      return false;
    }
    if (DemoReasonMaskedCount() != 2) {
      if (error != nullptr) {
        *error = "demo route content: reason masked count drift rejected";
      }
      return false;
    }
    std::string detail;
    if (!CheckDemoRouteJsonNumbers(DemoSegmentJson(), DemoSegmentJsonSize(),
                                   &detail)) {
      if (error != nullptr) {
        *error = "demo route content: segment numerics rejected";
      }
      return false;
    }
    for (std::size_t k = 0; k < DemoReasonMaskedCount(); ++k) {
      const char* chunk = DemoReasonMaskedChunk(k);
      if (chunk == nullptr ||
          !CheckDemoRouteJsonNumbers(chunk, std::strlen(chunk), &detail)) {
        if (error != nullptr) {
          *error = "demo route content: reason masked chunk rejected";
        }
        return false;
      }
    }
    if (kReasonMaskedSizes[0] != 41 || kReasonMaskedSizes[1] != 30) {
      if (error != nullptr) {
        *error = "demo route content: reason masked size drift rejected";
      }
      return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

std::string GuessPolygonHashHex(const char* bytes, std::size_t len) {
  try {
    if (bytes == nullptr || len == 0) {
      return "00000000";
    }
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < len; ++i) {
      hash ^= static_cast<unsigned char>(bytes[i]);
      hash *= 16777619u;
    }
    char out[9];
    std::snprintf(out, sizeof(out), "%08x", hash);
    return std::string(out, 8);
  } catch (...) {
    return "00000000";
  }
}

bool PlayGuessRound(const std::string& photoId, GuessPoint point,
                    GuessRoundResult* out, std::string* error) {
  try {
    if (out == nullptr) {
      if (error != nullptr) {
        *error = "playGuessRound requires a non-empty photoId";
      }
      return false;
    }
    // Mirrors `playGuessRound requires a non-empty photoId`
    // (guessMask.ts): empty or all-whitespace photoIds fail closed.
    bool blank = photoId.empty();
    for (char c : photoId) {
      if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
        blank = false;
        break;
      }
    }
    if (blank) {
      if (error != nullptr) {
        *error = "playGuessRound requires a non-empty photoId";
      }
      return false;
    }
    // Mirrors `playGuessRound requires a finite { x, y } guess point`.
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
      if (error != nullptr) {
        *error = "playGuessRound requires a finite { x, y } guess point";
      }
      return false;
    }
    // Resolve the round through the mirrored `segment` route bytes (the
    // native analogue of `masksFromSegment(resolveSegmentBody(...))`):
    // mask-1 is matches[0].mask (tap-region truth), mask-2 is
    // suggestions[0] (first dashed alternative). Error names mirror
    // guessMask.ts, not its JS.
    const char* segment = DemoSegmentJson();
    const std::size_t segmentLen = DemoSegmentJsonSize();
    if (segment == nullptr || segmentLen == 0) {
      if (error != nullptr) {
        *error = "guessMask: segment route returned no body";
      }
      return false;
    }
    std::string label;
    if (!ExtractMirrorLabel(segment, segmentLen, &label)) {
      if (error != nullptr) {
        *error = "guessMask: segment route returned no masks";
      }
      return false;
    }
    std::vector<GuessPolygonPoint> poly1;
    std::size_t begin1 = 0;
    std::size_t end1 = 0;
    if (!ExtractMirrorPoints(segment, segmentLen, 0, &poly1, &begin1,
                             &end1)) {
      if (error != nullptr) {
        *error = "guessMask: unparseable segment polygon";
      }
      return false;
    }
    std::vector<GuessPolygonPoint> poly2;
    std::size_t begin2 = 0;
    std::size_t end2 = 0;
    if (!ExtractMirrorPoints(segment, segmentLen, 1, &poly2, &begin2,
                             &end2)) {
      if (error != nullptr) {
        *error = "guessMask: segment route returned no suggestions";
      }
      return false;
    }
    // The mirrored `reason` route masked stream (native analogue of
    // `resolveReasonChunks`): empty means no masked stream, fail closed.
    if (DemoReasonMaskedCount() == 0) {
      if (error != nullptr) {
        *error = "guessMask: reason route returned no masked stream";
      }
      return false;
    }
    std::vector<std::string> chunks;
    for (std::size_t k = 0; k < DemoReasonMaskedCount(); ++k) {
      const char* chunk = DemoReasonMaskedChunk(k);
      if (chunk == nullptr || chunk[0] == '\0') {
        if (error != nullptr) {
          *error = "guessMask: reason route returned no masked stream";
        }
        return false;
      }
      chunks.emplace_back(chunk);
    }
    // Hidden mask fixed per round, never the visitor's guess: deterministic
    // pick over the routed masks, mirroring
    // `masks[photoId.length % masks.length]` in guessMask.ts. The guess
    // point only routes (validated finite above); it never selects.
    const bool hiddenIsFirst = (photoId.size() % 2) == 0;
    GuessRoundResult result;
    result.maskId = hiddenIsFirst ? "mask-1" : "mask-2";
    result.maskLabel = label;
    result.polygon = hiddenIsFirst ? poly1 : poly2;
    result.reasonChunks = chunks;
    result.shelfRenderMode = "by_object";
    // T4 marker triple, in order: route resolve → round (poly hash over
    // the hidden mask's raw routed bytes) → reveal. Proves the content
    // followed the route; repaint-only legs emit none of these.
    const std::size_t hashBegin = hiddenIsFirst ? begin1 : begin2;
    const std::size_t hashEnd = hiddenIsFirst ? end1 : end2;
    const std::string polyHash = GuessPolygonHashHex(
        segment + hashBegin, hashEnd > hashBegin ? hashEnd - hashBegin : 0);
    char chunkCount[32];
    std::snprintf(chunkCount, sizeof(chunkCount), "%u",
                  static_cast<unsigned>(chunks.size()));
    MuseGuessLog("muse_guess: route-resolve tag=segment ok");
    MuseGuessLog("muse_guess: round mask=" + result.maskId +
                 " polyHash=" + polyHash +
                 " reasonChunks=" + chunkCount);
    MuseGuessLog("muse_guess: reveal shelf=by_object");
    *out = result;
    return true;
  } catch (...) {
    if (error != nullptr) {
      *error = "playGuessRound: round failed closed";
    }
    return false;
  }
}

}  // namespace runtime

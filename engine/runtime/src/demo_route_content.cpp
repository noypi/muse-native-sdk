// Native demo route-content provider (engine-eval T1; FR-MUSE-002,
// FR-MUSE-004, RULE-MUSE-006).
// Self-contained TU: C++17 + toolchain only, plus the public runtime
// headers it registers through (mirrors the js_runtime.cpp TU discipline:
// no engine/JS headers, no network, no JNI, no upstream dial).

#include "runtime/demo_route_content.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "runtime/content_registry.h"
#include "runtime/gallery_present.h"

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

}  // namespace runtime

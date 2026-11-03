// Batch 1 RED pin — T1 (demo-fallback-boot, GR-08 T1.1).
//
// Gallery gate fails closed NAMED on kinds outside its known set: unknown
// component kinds must never be silently substituted (no tonal fallback card,
// no pixels touched) and the diagnostic must name the kind. Binds ONLY to the
// settled plan Interface `IsKnownGalleryComponent(kind: string): bool` (used
// as the known-set oracle) and the public raster entry
// `RasterGalleryPage` in `runtime/gallery_present.h`.
//
// Spec refs: FR-SAMPLE-009/010, AC-SAMPLE-023 (fail closed, named).
//
// Hermetic: C++17 + committed headers only; in-memory rows, sentinel-filled
// pixel buffer, no GPU/window/network/env/filesystem/services.
//
// Expected RED (pre-T4): the raster path substitutes a tonal fallback card
// for unknown kinds (gallery_present.cpp PaintComponent fallback), so
// RasterGalleryPage returns >0 with no diagnostic — the six probe cases
// below FAIL until the coder extends the fail-closed gate to the raster
// path. Known-kind oracle legs stay GREEN (they guard T4 against
// over-blocking).
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "muse/text/font_discovery.h"
#include "runtime/gallery_present.h"

#define RUN_CASE(name)                  \
  do {                                  \
    if (!(name)()) {                    \
      std::printf("FAIL %s\n", #name);  \
      ++failures;                       \
    } else {                            \
      std::printf("PASS %s\n", #name);  \
    }                                   \
  } while (0)

namespace runtime {
// Production declaration (coder-owned gallery_present.cpp). Assumed symbol
// per the plan's `Interface:` line: unknown -> false + named diagnostic
// `unknown gallery component kind '<kind>'`; never substitute.
bool IsKnownGalleryComponent(const std::string& comp);
}  // namespace runtime

namespace {

// Plan acceptance probes: kinds outside the retained known set. These stay
// unknown after T4 (T4 registers demo kinds; these probes are never demo
// content), so fail-closed legs are stable across the fix.
const char* kUnknownProbes[] = {
    "lens", "string", "ErrorSnackbar",
    "AnimatableProp", "InlineError", "unknown",
};

const char* kKnownKinds[] = {"Button", "Card", "TopAppBar"};

runtime::GalleryRow ProbeRow(const char* kind) {
  runtime::GalleryRow row;
  row.label = std::string("Probe ") + kind;
  row.comp = kind;
  row.desc = std::string("Type ") + kind + " unit=u";
  row.dark = false;
  row.header = false;
  return row;
}

// Fail-closed contract for one probe kind: zero rows drawn, untouched
// pixels, and a diagnostic naming the kind. Returns true only when all
// three hold.
bool ProbeFailsClosedNamed(const char* kind) {
  std::vector<runtime::GalleryRow> rows;
  rows.push_back(ProbeRow(kind));
  const int w = 720;
  const int h = 1600;
  const std::uint32_t kSentinel = 0xDEADBEEFu;
  std::vector<std::uint32_t> px(static_cast<std::size_t>(w) * h, kSentinel);
  std::string error;
  int drawn = 0;
  try {
    drawn =
        runtime::RasterGalleryPage(rows, 0, px.data(), w, w, h, &error);
  } catch (...) {
    std::printf("  detail: kind '%s' threw (must never throw)\n", kind);
    return false;
  }
  const std::string want = std::string("unknown gallery component kind '") +
                           kind + "'";
  if (drawn != 0) {
    std::printf("  detail: kind '%s' substituted (%d rows drawn, want 0)\n",
                kind, drawn);
    return false;
  }
  if (error.find(want) == std::string::npos) {
    std::printf("  detail: kind '%s' unnamed (error='%s', want '%s')\n",
                kind, error.c_str(), want.c_str());
    return false;
  }
  for (std::uint32_t v : px) {
    if (v != kSentinel) {
      std::printf("  detail: kind '%s' touched pixels (substitute paint)\n",
                  kind);
      return false;
    }
  }
  return true;
}

bool check_probe_lens_fail_closed() { return ProbeFailsClosedNamed("lens"); }
bool check_probe_string_fail_closed() {
  return ProbeFailsClosedNamed("string");
}
bool check_probe_errorsnackbar_fail_closed() {
  return ProbeFailsClosedNamed("ErrorSnackbar");
}
bool check_probe_animatableprop_fail_closed() {
  return ProbeFailsClosedNamed("AnimatableProp");
}
bool check_probe_inlineerror_fail_closed() {
  return ProbeFailsClosedNamed("InlineError");
}
bool check_probe_unknown_fail_closed() {
  return ProbeFailsClosedNamed("unknown");
}

// Oracle legs: the known set is unchanged by this pin. Unknown probes are
// not known; retained kinds stay known (T4 must not over-block).
bool check_unknown_probes_not_known() {
  for (const char* kind : kUnknownProbes) {
    if (runtime::IsKnownGalleryComponent(kind)) {
      std::printf("  detail: probe '%s' reported known\n", kind);
      return false;
    }
  }
  return true;
}

bool check_retained_kinds_stay_known() {
  for (const char* kind : kKnownKinds) {
    if (!runtime::IsKnownGalleryComponent(kind)) {
      std::printf("  detail: retained kind '%s' reported unknown\n", kind);
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  // Same hermetic shaping lane as gallery_present_test: unopenable
  // in-memory bytes route shaping through the deterministic stub (no host
  // fonts, no downloads, no device). Fail-closed legs must fail before
  // shaping matters; the sentinel-pixel leg is shaping-independent.
  const std::uint8_t kNoFont[] = {0x4E, 0x4F, 0x54, 0x41, 0x46,
                                  0x4F, 0x4E, 0x54, 0x00, 0xFF};
  muse::text::RegisterTestOnlyMemoryFont(kNoFont, sizeof(kNoFont));
  int failures = 0;
  RUN_CASE(check_probe_lens_fail_closed);
  RUN_CASE(check_probe_string_fail_closed);
  RUN_CASE(check_probe_errorsnackbar_fail_closed);
  RUN_CASE(check_probe_animatableprop_fail_closed);
  RUN_CASE(check_probe_inlineerror_fail_closed);
  RUN_CASE(check_probe_unknown_fail_closed);
  RUN_CASE(check_unknown_probes_not_known);
  RUN_CASE(check_retained_kinds_stay_known);
  if (failures > 0) {
    std::printf("fallback_gate_kinds_test: %d failing\n", failures);
    return 1;
  }
  std::printf("fallback_gate_kinds_test: all passing\n");
  return 0;
}

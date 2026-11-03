// Batch 1 RED pin — T1 (fallback-kinds, GR-08 T1.1).
//
// Unknown/demo-specific kinds fail closed NAMED: they must never resolve,
// never be silently substituted, and the diagnostic must name the kind.
// Binds ONLY to the settled plan Interface
// `IsKnownGalleryComponent(kind: string): bool` (known-set oracle) and the
// public raster entry `RasterGalleryPage` in `runtime/gallery_present.h`.
//
// Probes (plan T1 acceptance, 8): `lens`, `string`, `ErrorSnackbar`,
// `unknown`, `Field`, `DemoState`, `guessMask`, `AnimatableProp`.
//
// Spec refs: AC-SAMPLE-023 (fail closed, named).
//
// Hermetic: C++17 + committed runtime headers only; in-memory rows,
// sentinel-filled pixel buffer, no GPU/window/network/env/filesystem.
//
// Expected RED (pre-T3): the raster path substitutes a fallback card for
// unknown kinds, so RasterGalleryPage returns >0 with no named diagnostic —
// the eight probe cases below FAIL until the coder fixes the demo tree
// (Batch 2) AND the gate names every unknown kind. Oracle legs stay GREEN
// (they guard the fix against over-blocking the T2 generic targets:
// Image, TextInput, Card, SnackbarHost).
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "muse/text/font_discovery.h"
#include "runtime/gallery_present.h"

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

namespace runtime {
// Production declaration (coder-owned gallery_present.cpp). Assumed symbol
// per the plan's `Interface:` line: unknown -> false + named diagnostic
// `unknown gallery component kind '<kind>'`; never substitute, never throw.
bool IsKnownGalleryComponent(const std::string& comp);
}  // namespace runtime

namespace {

// Plan T1 acceptance probes: demo-specific / unknown kinds. None of these
// is demo content after the Batch 2 remap (demo emits only Image,
// TextInput, Card, SnackbarHost), so these legs stay fail-closed GREEN
// after the fix too.
const char* kUnknownProbes[] = {
    "lens", "string", "ErrorSnackbar", "unknown",
    "Field", "DemoState", "guessMask", "AnimatableProp",
};

// T2 generic targets: the remap lands here, so the gate must keep them
// known (anti-over-blocking oracle).
const char* kRetainedKinds[] = {"Image", "TextInput", "Card", "SnackbarHost"};

runtime::GalleryRow ProbeRow(const char* kind) {
  runtime::GalleryRow row;
  row.label = std::string("Probe ") + kind;
  row.comp = kind;
  row.desc = std::string("Type ") + kind + " unit=u";
  row.dark = false;
  row.header = false;
  return row;
}

// Fail-closed NAMED contract for one probe kind: zero rows drawn,
// untouched pixels, and a diagnostic naming the kind. Never throws.
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
  const std::string want =
      std::string("unknown gallery component kind '") + kind + "'";
  if (drawn != 0) {
    std::printf("  detail: kind '%s' substituted (%d rows drawn, want 0)\n",
                kind, drawn);
    return false;
  }
  if (error.find(want) == std::string::npos) {
    std::printf("  detail: kind '%s' unnamed (error='%s', want '%s')\n", kind,
                error.c_str(), want.c_str());
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
bool check_probe_unknown_fail_closed() {
  return ProbeFailsClosedNamed("unknown");
}
bool check_probe_field_fail_closed() { return ProbeFailsClosedNamed("Field"); }
bool check_probe_demostate_fail_closed() {
  return ProbeFailsClosedNamed("DemoState");
}
bool check_probe_guessmask_fail_closed() {
  return ProbeFailsClosedNamed("guessMask");
}
bool check_probe_animatableprop_fail_closed() {
  return ProbeFailsClosedNamed("AnimatableProp");
}

// Oracle legs: probes are never known; T2 generic targets stay known.
bool check_probes_never_known() {
  for (const char* kind : kUnknownProbes) {
    if (runtime::IsKnownGalleryComponent(kind)) {
      std::printf("  detail: probe '%s' reported known (must fail closed)\n",
                  kind);
      return false;
    }
  }
  if (runtime::IsKnownGalleryComponent("")) {
    std::printf("  detail: empty comp reported known\n");
    return false;
  }
  return true;
}

bool check_remap_targets_stay_known() {
  for (const char* kind : kRetainedKinds) {
    if (!runtime::IsKnownGalleryComponent(kind)) {
      std::printf("  detail: remap target '%s' reported unknown\n", kind);
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  // Same hermetic shaping lane as gallery_present_test: unopenable
  // in-memory bytes route shaping through the deterministic stub (no host
  // fonts, no downloads, no device).
  const std::uint8_t kNoFont[] = {0x4E, 0x4F, 0x54, 0x41, 0x46,
                                  0x4F, 0x4E, 0x54, 0x00, 0xFF};
  muse::text::RegisterTestOnlyMemoryFont(kNoFont, sizeof(kNoFont));
  int failures = 0;
  RUN_CASE(check_probe_lens_fail_closed);
  RUN_CASE(check_probe_string_fail_closed);
  RUN_CASE(check_probe_errorsnackbar_fail_closed);
  RUN_CASE(check_probe_unknown_fail_closed);
  RUN_CASE(check_probe_field_fail_closed);
  RUN_CASE(check_probe_demostate_fail_closed);
  RUN_CASE(check_probe_guessmask_fail_closed);
  RUN_CASE(check_probe_animatableprop_fail_closed);
  RUN_CASE(check_probes_never_known);
  RUN_CASE(check_remap_targets_stay_known);
  if (failures > 0) {
    std::printf("gallery_gate_kind_test: %d failing\n", failures);
    return 1;
  }
  std::printf("gallery_gate_kind_test: all passing\n");
  return 0;
}

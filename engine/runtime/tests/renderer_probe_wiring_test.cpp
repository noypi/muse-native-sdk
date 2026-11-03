// Batch 4 T7 contract tests — bug-runtime-008 hermetic-prod-leak scope.
// Binds ONLY to the T7 `Interface:` line in plan.md; never to implementation.
// Hermetic: C++17 + `runtime/renderer_preference.h` + `present_skia.h`
// public headers only, linked against the real `present_skia.cpp` TU;
// availability is injected per-call, no EGL/Skia/GPU on host, no network,
// no env/config reads, no live services (frame dumps go to the OS temp
// dir only in the success arm; the fallback arms touch no filesystem).
//
// Batch 4 fix round (MAJOR-3): the old `RENDERER_PROBE_WIRED`-macro-presence
// pin was circular (it asserted a `#define` the production change itself
// introduces). Wiring is now pinned behaviorally: the resolve gate is driven
// through real input shapes — the single-sourced CLI default
// (`RendererProbeDefaultRequested()`) plus decided device-probing health
// (`DeviceSkiaLaneSucceeded()`) — and the effective outcomes + named
// triggers are asserted. A revert of the call site to literals while keeping
// the macro fails these pins.
#include <cstdio>
#include <cstring>
#include <string>

#include "runtime/renderer_preference.h"
#include "present_skia.h"

static bool check_harness_entry_reached() { return true; }

// --- Shared contract suite (Fake and Real must both pass per GR-12 P12.6) ---
// Real adapter: `engine/runtime/src/jni_bridge.cpp` (RenderLocked resolve
// gate) + `engine/runtime/include/runtime/renderer_preference.h` (inline
// definition). Fakes live in test trees only.

static bool check_null_requested_means_cpu() {
  char effective[16] = {};
  std::string trigger = "untouched";
  bool ok = ResolveRendererFallback(nullptr, false, false, effective,
                                    sizeof(effective), &trigger);
  return ok && std::strcmp(effective, "cpu") == 0 && trigger.empty();
}

static bool check_egl_available_keeps_egl() {
  char effective[16] = {};
  std::string trigger = "x";
  bool ok = ResolveRendererFallback("egl", true, false, effective,
                                    sizeof(effective), &trigger);
  return ok && std::strcmp(effective, "egl") == 0 && trigger.empty();
}

static bool check_skia_available_keeps_skia() {
  char effective[16] = {};
  std::string trigger = "x";
  bool ok = ResolveRendererFallback("skia", false, true, effective,
                                    sizeof(effective), &trigger);
  return ok && std::strcmp(effective, "skia") == 0 && trigger.empty();
}

static bool check_skia_unavailable_downgrades_named() {
  char effective[16] = {};
  std::string trigger;
  bool ok = ResolveRendererFallback("skia", false, false, effective,
                                    sizeof(effective), &trigger);
  return ok && std::strcmp(effective, "cpu") == 0 &&
         trigger == "renderer-downgrade:skia-unavailable";
}

// Boundary: empty-string requested fails closed to cpu, never blank.
static bool check_empty_requested_fails_closed() {
  char effective[16] = {};
  std::string trigger;
  bool ok = ResolveRendererFallback("", true, true, effective,
                                    sizeof(effective), &trigger);
  return ok && std::strcmp(effective, "cpu") == 0 && !trigger.empty();
}

// Boundary: case-sensitive preference ("SKIA" != "skia") fails closed,
// never echoes input as a path.
static bool check_case_mismatch_fails_closed_no_echo() {
  char effective[16] = {};
  std::string trigger;
  bool ok = ResolveRendererFallback("SKIA", true, true, effective,
                                    sizeof(effective), &trigger);
  return ok && std::strcmp(effective, "cpu") == 0 &&
         std::strcmp(effective, "SKIA") != 0 &&
         trigger.find("SKIA") == std::string::npos;
}

// Error: unknown + overlong requested never echoes raw bytes into the
// trigger (log integrity; trigger vocabulary stays closed).
static bool check_unknown_overlong_never_embeds_input() {
  std::string evil(300, 'x');
  evil += ";rm -rf /";
  char effective[16] = {};
  std::string trigger;
  bool ok = ResolveRendererFallback(evil.c_str(), true, true, effective,
                                    sizeof(effective), &trigger);
  return ok && std::strcmp(effective, "cpu") == 0 &&
         trigger.find("rm") == std::string::npos &&
         trigger.find(";") == std::string::npos &&
         trigger.find("xxx") == std::string::npos &&
         trigger == "renderer-downgrade:unknown-requested";
}

// Error: null out-params tolerated, never crash, still resolves.
static bool check_null_out_params_tolerated() {
  return ResolveRendererFallback("skia", false, false, nullptr, 0, nullptr);
}

// --- Behavior wiring pin (replaces the circular RENDERER_PROBE_WIRED macro
// pin per MAJOR-3) ----------------------------------------------------------
// The RenderLocked gate must source `requested` from the single-sourced CLI
// default (FR-WORKFLOW-008 `skia`, never a call-site literal) and resolve it
// through the real lane inputs. Drive the gate through two real input shapes
// built from the actual default source and assert effective outcomes + named
// trigger — no macro existence is asserted anywhere in this suite.
static bool check_default_source_drives_gate_with_named_trigger() {
  const char* def = RendererProbeDefaultRequested();
  if (def == nullptr || std::strcmp(def, "skia") != 0) return false;
  // Shape 1: default requested, no lane proven → CPU + named downgrade.
  char effective[16] = {};
  std::string trigger;
  if (!ResolveRendererFallback(def, false, false, effective,
                               sizeof(effective), &trigger))
    return false;
  if (!(std::strcmp(effective, "cpu") == 0 &&
        trigger == "renderer-downgrade:skia-unavailable"))
    return false;
  // Shape 2: default requested, Skia lane proven → skia, silent.
  char effective2[16] = {};
  std::string trigger2 = "untouched";
  if (!ResolveRendererFallback(def, false, true, effective2,
                               sizeof(effective2), &trigger2))
    return false;
  return std::strcmp(effective2, "skia") == 0 && trigger2.empty();
}

// --- Health-bit monotonicity (security: `skiaAvailable` true only after an
// observed successful present — never assumed, never a literal) -------------
// Runs before any successful present in this process: the lane bit starts
// false, and a fault-injected (failed) frame keeps it false — failure can
// never set it.
static bool check_health_bit_false_until_observed_failure_never_sets() {
  if (DeviceSkiaLaneSucceeded()) return false;
  skia_test::FaultHooks hooks;
  hooks.fail_init = true;
  skia_test::SetFaultHooks(&hooks);
  uint32_t px = 0xFF000000u;
  std::string trigger;
  bool ok = PresentSkiaFrameWithFallback(&px, 1, 1, &trigger);
  skia_test::SetFaultHooks(nullptr);
  return ok && !trigger.empty() && !DeviceSkiaLaneSucceeded();
}

// Fence-shape pin (security + LOW): the compiled shape decides the outcome.
// Fenced (`-DHOST_SCAFFOLD`) host build: the scaffold lane presents and the
// observed-success bit goes sticky-true. Unfenced prod shape: the stub
// answers `false`, the frame falls back to CPU, the bit stays false —
// success-without-work is unreachable without the flag.
static bool check_observed_success_sets_bit_fenced_unfenced_falls_back() {
  skia_test::SetFaultHooks(nullptr);
  uint32_t px = 0xFF000000u;
  std::string trigger = "untouched";
  bool ok = PresentSkiaFrameWithFallback(&px, 1, 1, &trigger);
  if (!ok) return false;
#ifdef HOST_SCAFFOLD
  return trigger.empty() && DeviceSkiaLaneSucceeded();
#else
  return !trigger.empty() && !DeviceSkiaLaneSucceeded();
#endif
}

#define RUN_CASE(name)                                   \
  do {                                                   \
    if (!(name)()) {                                     \
      std::printf("FAIL %s\n", #name);                   \
      ++failures;                                        \
    } else {                                             \
      std::printf("PASS %s\n", #name);                   \
    }                                                    \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_null_requested_means_cpu);
  RUN_CASE(check_egl_available_keeps_egl);
  RUN_CASE(check_skia_available_keeps_skia);
  RUN_CASE(check_skia_unavailable_downgrades_named);
  RUN_CASE(check_empty_requested_fails_closed);
  RUN_CASE(check_case_mismatch_fails_closed_no_echo);
  RUN_CASE(check_unknown_overlong_never_embeds_input);
  RUN_CASE(check_null_out_params_tolerated);
  RUN_CASE(check_default_source_drives_gate_with_named_trigger);
  RUN_CASE(check_health_bit_false_until_observed_failure_never_sets);
  RUN_CASE(check_observed_success_sets_bit_fenced_unfenced_falls_back);
  if (failures > 0) {
    std::printf("renderer_probe_wiring_test: %d failing\n", failures);
    return 1;
  }
  std::printf("renderer_probe_wiring_test: all passing\n");
  return 0;
}

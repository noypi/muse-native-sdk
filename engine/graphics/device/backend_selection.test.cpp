// Batch 1 contract tests — T1: select_backend probe (FR-GRAPHICS-003).
// Binds ONLY to the T1 `Interface:` line in plan.md; never to implementation.
// Hermetic: C++17 + planned graphics headers only; no network, no env/
// config reads, no filesystem, no live services.
// Expected state before implementation: RED — `device/backend_probe.h`
// does not exist, so compilation fails (missing behavior, not a malformed
// test). The coder provides the headers/sources; these tests then compile
// and run unmodified (call-site reconciliation only, per ASSUMPTIONs).
#include <cstdio>
#include <cstring>
#include <string>

#include "device/backend_probe.h"

// --- harness cases (repo convention, cf. engine/runtime/tests) --------------

static bool check_harness_entry_reached() { return true; }

static bool check_all_green_returns_zero() {
  const int greenFailures = 0;
  return greenFailures == 0;
}

static bool check_failure_protocol_shape() {
  const char* failingName = "";
  return failingName != nullptr && std::strlen(failingName) == 0;
}

// --- T1 helpers ------------------------------------------------------------
// ASSUMPTION (A-T1-SHAPE): C++ spelling of the plan interface. Plan line:
// `select_backend(probe: DeviceProbe): BackendSelection` with
// `DeviceProbe{vk13_dynamic_rendering, vk110_available, gles32_available,
// driver_broken_vulkan}`, returning exactly one of `VULKAN13_DYNAMIC |
// VULKAN11_EMULATION | GLES32`, error `NO_VIABLE_BACKEND`. The plan/spec do
// not settle namespace, enum class, or error channel; tests assume
// `graphics::DeviceProbe` aggregate with those four bool fields,
// `graphics::select_backend(const DeviceProbe&) -> graphics::BackendSelection`
// (`enum class` with the three named enumerators), and error signalled by
// throwing `graphics::NoViableBackendError` whose `what()` is exactly
// "NO_VIABLE_BACKEND". If the planner/coder settles a different channel
// (e.g. status return), update call sites only — expectations are unchanged.

static graphics::DeviceProbe make_probe(bool vk13, bool vk11, bool gles,
                                        bool broken) {
  graphics::DeviceProbe p;
  p.vk13_dynamic_rendering = vk13;
  p.vk110_available = vk11;
  p.gles32_available = gles;
  p.driver_broken_vulkan = broken;
  return p;
}

static bool expect_selection(const graphics::DeviceProbe& p,
                             graphics::BackendSelection want) {
  try {
    return graphics::select_backend(p) == want;
  } catch (...) {
    return false;
  }
}

static bool expect_no_viable_backend(const graphics::DeviceProbe& p) {
  try {
    graphics::select_backend(p);
    return false;
  } catch (const graphics::NoViableBackendError& e) {
    return std::string(e.what()) == "NO_VIABLE_BACKEND";
  } catch (...) {
    return false;
  }
}

// --- T1 behavior cases (AC-GRAPHICS-005, NFR-GRAPHICS-001/AC-001) ------------

// AC-005 happy path: advertised 1.3 dynamic rendering on a healthy driver.
static bool check_vk13_advertised_selects_vulkan13_dynamic() {
  return expect_selection(make_probe(true, true, true, false),
                          graphics::BackendSelection::VULKAN13_DYNAMIC);
}

// AC-001: 1.1 baseline — no 1.3, Vulkan 1.1 available.
static bool check_vk11_only_selects_vulkan11_emulation() {
  return expect_selection(make_probe(false, true, true, false),
                          graphics::BackendSelection::VULKAN11_EMULATION);
}

// AC-001: GLES 3.2 fallback when no Vulkan path is available.
static bool check_gles_only_selects_gles32() {
  return expect_selection(make_probe(false, false, true, false),
                          graphics::BackendSelection::GLES32);
}

// FR-GRAPHICS-003 rationale: broken Vulkan driver degrades to GLES 3.2 even
// when Vulkan 1.3 advertises features.
static bool check_broken_vulkan_driver_selects_gles32() {
  return expect_selection(make_probe(true, true, true, true),
                          graphics::BackendSelection::GLES32);
}

// AC-005 error path: nothing viable (incl. broken Vulkan with no GLES).
static bool check_no_backend_yields_no_viable_backend() {
  if (!expect_no_viable_backend(make_probe(false, false, false, false)))
    return false;
  return expect_no_viable_backend(make_probe(true, true, false, true));
}

// AC-005 invariant: exactly one backend across the full input space — the
// probe never returns two backends, never picks Vulkan 1.3 without a
// successful probe (AC-007 "no path assumes 1.3"), and never picks a broken
// Vulkan path.
static bool check_exactly_one_backend_across_all_combos() {
  for (int mask = 0; mask < 16; ++mask) {
    const bool vk13 = (mask & 1) != 0;
    const bool vk11 = (mask & 2) != 0;
    const bool gles = (mask & 4) != 0;
    const bool broken = (mask & 8) != 0;
    const graphics::DeviceProbe p = make_probe(vk13, vk11, gles, broken);
    bool selected13 = false, selected11 = false, selectedGles = false;
    bool threw = false;
    try {
      const graphics::BackendSelection s = graphics::select_backend(p);
      selected13 = (s == graphics::BackendSelection::VULKAN13_DYNAMIC);
      selected11 = (s == graphics::BackendSelection::VULKAN11_EMULATION);
      selectedGles = (s == graphics::BackendSelection::GLES32);
    } catch (const graphics::NoViableBackendError&) {
      threw = true;
    } catch (...) {
      return false;  // unknown error channel: fail, do not swallow.
    }
    const int count =
        (selected13 ? 1 : 0) + (selected11 ? 1 : 0) + (selectedGles ? 1 : 0);
    if (threw) {
      if (count != 0) return false;
      // Error is only legal when no healthy path exists.
      const bool healthy = (!broken && (vk13 || vk11)) || gles;
      if (healthy) return false;
    } else {
      if (count != 1) return false;  // exactly one backend.
      if (selected13 && (!vk13 || broken)) return false;  // never assume 1.3.
      if ((selected13 || selected11) && broken) return false;
      if (selectedGles && !gles) return false;
    }
  }
  return true;
}

// Security (GR-05 R5.3): the error shape carries no probe internals —
// exactly "NO_VIABLE_BACKEND", no driver/vendor dump in the message.
static bool check_error_shape_leaks_no_internals() {
  try {
    graphics::select_backend(make_probe(false, false, false, false));
    return false;
  } catch (const graphics::NoViableBackendError& e) {
    return std::string(e.what()) == "NO_VIABLE_BACKEND";
  } catch (...) {
    return false;
  }
}

#define RUN_CASE(name)                                       \
  do {                                                       \
    if (!(name)()) {                                         \
      std::printf("FAIL %s\n", #name);                       \
      ++failures;                                            \
    } else {                                                 \
      std::printf("PASS %s\n", #name);                       \
    }                                                        \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_all_green_returns_zero);
  RUN_CASE(check_failure_protocol_shape);
  RUN_CASE(check_vk13_advertised_selects_vulkan13_dynamic);
  RUN_CASE(check_vk11_only_selects_vulkan11_emulation);
  RUN_CASE(check_gles_only_selects_gles32);
  RUN_CASE(check_broken_vulkan_driver_selects_gles32);
  RUN_CASE(check_no_backend_yields_no_viable_backend);
  RUN_CASE(check_exactly_one_backend_across_all_combos);
  RUN_CASE(check_error_shape_leaks_no_internals);
  if (failures > 0) {
    std::printf("backend_selection.test: %d failing\n", failures);
    return 1;
  }
  std::printf("backend_selection.test: all passing\n");
  return 0;
}

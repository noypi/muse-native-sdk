// Batch 3 contract tests — T6: Khronos validation layer gated to dev/profile
// builds only (FR-GRAPHICS-005, AC-011). Binds ONLY to the T6 `Interface:`
// line in plan.md; never to implementation.
// Hermetic: C++17 + planned graphics headers only; no network, no env/
// config reads, no live services. The only filesystem touch is a `/tmp`
// fixture file the test itself creates (mirroring the documented
// build-config.json schema) and removes; the shipped repo config is never
// read here — the TC-011 grep against the shipped file is documented as a
// static check the orchestrator runs at verify time (see ASSUMPTIONs).
// Expected state before implementation: RED — `caps/validation_gate.h`
// does not exist, so compilation fails (missing behavior, not a malformed
// test). The coder provides the header/source; these tests then compile
// and run unmodified (call-site reconciliation only, per ASSUMPTIONs).
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "caps/validation_gate.h"

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

// --- T6 helpers ------------------------------------------------------------
// ASSUMPTION (A-T6-SHAPE): C++ spelling of the plan interface. Plan line:
// `validation_enabled(build: BuildType): bool` — true for DEV|PROFILE,
// false for RELEASE. The plan/spec do not settle namespace or error channel;
// tests assume `graphics::BuildType{DEV, PROFILE, RELEASE}` (exact T7
// committed spelling from `shaders/shader_bundle.h`) and free function
// `graphics::validation_enabled(BuildType) -> bool`. The test includes ONLY
// `caps/validation_gate.h`, so the coder must keep a single `BuildType`
// definition visible there (reuse the T7 spelling, do not redeclare it in a
// way that clashes when both headers are included). If the planner/coder
// settles different spellings, update call sites only — expectations are
// unchanged (genuine `validation_enabled` signature clash → planner per
// GR-08 T1.10).

// --- T6 behavior cases (AC-GRAPHICS-011) --------------------------------------

// AC-011: dev builds enable the Khronos validation layer.
static bool check_validation_enabled_dev_true() {
  return graphics::validation_enabled(graphics::BuildType::DEV) == true;
}

// AC-011: profile builds enable the Khronos validation layer.
static bool check_validation_enabled_profile_true() {
  return graphics::validation_enabled(graphics::BuildType::PROFILE) == true;
}

// AC-011: release builds never enable the layer — the gate is the single
// choke point, so release can never load VK_LAYER_KHRONOS_validation.
static bool check_validation_enabled_release_false() {
  return graphics::validation_enabled(graphics::BuildType::RELEASE) == false;
}

// AC-011 totality: the gate is deterministic and exhaustive — every settled
// build flavour maps to exactly one verdict, and only DEV|PROFILE are true.
static bool check_gate_deterministic_and_exhaustive() {
  using graphics::BuildType;
  using graphics::validation_enabled;
  const bool dev1 = validation_enabled(BuildType::DEV);
  const bool dev2 = validation_enabled(BuildType::DEV);
  const bool prof = validation_enabled(BuildType::PROFILE);
  const bool rel = validation_enabled(BuildType::RELEASE);
  return dev1 == true && dev2 == dev1 && prof == true && rel == false;
}

// Security/robustness (GR-05 R5.3): an out-of-range build value fails closed
// — it must never enable validation (fail-closed default, no default-true).
static bool check_unknown_build_type_fails_closed() {
  const graphics::BuildType hostile =
      static_cast<graphics::BuildType>(99);
  return graphics::validation_enabled(hostile) == false;
}

// --- T6 config-shape cases (TC-011 grep predicate, AC-011) -------------------
// ASSUMPTION (A-T6-CONFIG): `engine/graphics/caps/build-config.json`
// declares the layer per build type; TC-011 is a grep proving
// `VK_LAYER_KHRONOS_validation` appears only on the dev/profile-gated path
// and never on the release path. The shipped file is coder-owned production
// config, so this hermetic test does NOT read it; instead it exercises the
// exact grep predicate against a self-created `/tmp` fixture mirroring the
// documented schema (A3 in plan.md). The orchestrator runs the real grep at
// verify time: `grep -R VK_LAYER_KHRONOS_validation
// engine/graphics/caps/build-config.json` must show the layer only under
// dev/profile sections. Fixture/config-schema drift → planner.

static const char* kFixturePath = "/tmp/t6_build_config_fixture.json";

static bool write_fixture() {
  std::ofstream out(kFixturePath, std::ios::trunc);
  if (!out) return false;
  out << "{\n"
      << "  \"version\": 1,\n"
      << "  \"validation_layers\": {\n"
      << "    \"DEV\": [\"VK_LAYER_KHRONOS_validation\"],\n"
      << "    \"PROFILE\": [\"VK_LAYER_KHRONOS_validation\"],\n"
      << "    \"RELEASE\": []\n"
      << "  }\n"
      << "}\n";
  out.close();
  return true;
}

static std::string read_fixture() {
  std::ifstream in(kFixturePath);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// TC-011 predicate: the layer is declared for dev/profile-gated configs.
static bool check_config_fixture_gates_layer_to_dev_profile() {
  if (!write_fixture()) return false;
  const std::string body = read_fixture();
  std::remove(kFixturePath);
  if (body.empty()) return false;
  const bool dev = body.find("\"DEV\": [\"VK_LAYER_KHRONOS_validation\"]") !=
                   std::string::npos;
  const bool prof =
      body.find("\"PROFILE\": [\"VK_LAYER_KHRONOS_validation\"]") !=
      std::string::npos;
  return dev && prof;
}

// TC-011 predicate, release side: the release section declares no layer, so
// a grep for the layer name must never match a release-gated load path.
static bool check_config_fixture_release_loads_no_layer() {
  if (!write_fixture()) return false;
  const std::string body = read_fixture();
  std::remove(kFixturePath);
  if (body.empty()) return false;
  // Release section is explicitly empty — no layer string on its load path.
  return body.find("\"RELEASE\": []") != std::string::npos;
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
  RUN_CASE(check_validation_enabled_dev_true);
  RUN_CASE(check_validation_enabled_profile_true);
  RUN_CASE(check_validation_enabled_release_false);
  RUN_CASE(check_gate_deterministic_and_exhaustive);
  RUN_CASE(check_unknown_build_type_fails_closed);
  RUN_CASE(check_config_fixture_gates_layer_to_dev_profile);
  RUN_CASE(check_config_fixture_release_loads_no_layer);
  if (failures > 0) {
    std::printf("build_config.test: %d failing\n", failures);
    return 1;
  }
  std::printf("build_config.test: all passing\n");
  return 0;
}

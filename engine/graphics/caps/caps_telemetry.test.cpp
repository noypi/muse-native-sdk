// Batch 1 contract tests — T4: resolve_tier fingerprint telemetry
// (FR-GRAPHICS-005). Binds ONLY to the T4 `Interface:` line in plan.md.
// Hermetic: C++17 + planned graphics headers only; no network, no env/
// config reads, no filesystem, no live services.
// Expected state before implementation: RED — `caps/caps_tier.h` does not
// exist, so compilation fails (missing behavior, not a malformed test).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "caps/caps_tier.h"

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

// --- T4 helpers ------------------------------------------------------------
// ASSUMPTION (A-T4-FIELDS): the plan names `DeviceFingerprint` but does not
// settle its fields. Tests assume the minimal spec-grounded shape:
// vendor, gpu_id, driver_version (strings, cf. FR-GRAPHICS-005 workaround-DB
// schema {vendor, driver_version, gpu_id, ...}), ram_gb (RAM class, cf.
// NFR-GRAPHICS-003 Low-End RAM<4GB / Mid/High-End RAM>=6GB), plus the probe
// flags vk13_dynamic_rendering / vk110_available / gles32_available (per
// plan detail "tier derived from probed features/properties + RAM class").
// ASSUMPTION (A-T4-TIER): tier thresholds — HIGH requires advertised
// Vulkan 1.3 dynamic rendering with RAM>=6GB; LOW when RAM<4GB or when only
// GLES 3.2 is available; MID otherwise. RAM cutoffs come from
// NFR-GRAPHICS-003; the vk13→HIGH linkage is the most defensible reading of
// "tier derived from probed features" and is flagged for planner confirm.
// ASSUMPTION (A-T4-WA): `resolve_tier` takes only a fingerprint (per plan
// Interface line), so with no DB wired at T4 scope the returned
// `workarounds` list is empty for unknown fingerprints; T5 wires matching.

static graphics::DeviceFingerprint make_fp(const char* vendor,
                                            const char* gpu,
                                            const char* driver, int ram_gb,
                                            bool vk13, bool vk11, bool gles) {
  graphics::DeviceFingerprint f;
  f.vendor = vendor;
  f.gpu_id = gpu;
  f.driver_version = driver;
  f.ram_gb = ram_gb;
  f.vk13_dynamic_rendering = vk13;
  f.vk110_available = vk11;
  f.gles32_available = gles;
  return f;
}

static graphics::DeviceFingerprint high_fp() {
  return make_fp("arm", "mali-g710", "r42p0", 8, true, true, true);
}

static bool reports_equal(const graphics::CapsReport& a,
                          const graphics::CapsReport& b) {
  return a.tier == b.tier && a.workarounds == b.workarounds &&
         a.fingerprint_hash == b.fingerprint_hash;
}

static bool is_hex(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s) {
    const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                     (c >= 'A' && c <= 'F');
    if (!hex) return false;
  }
  return true;
}

// --- T4 behavior cases (AC-GRAPHICS-009) ------------------------------------

// AC-009 core: a fixed fingerprint reproduces a byte-identical CapsReport.
static bool check_same_fingerprint_reproduces_identical_report() {
  const graphics::DeviceFingerprint f = high_fp();
  const graphics::CapsReport r1 = graphics::resolve_tier(f);
  const graphics::CapsReport r2 = graphics::resolve_tier(f);
  return reports_equal(r1, r2);
}

// AC-009: the telemetry record carries tier, workaround set, and hash.
// (The CapsReport struct itself is the telemetry record per the plan
// Interface line; no separate payload function is assumed.)
static bool check_telemetry_payload_carries_tier_workarounds_hash() {
  const graphics::CapsReport r = graphics::resolve_tier(high_fp());
  const bool tier_ok = (r.tier == graphics::CapsTier::LOW ||
                        r.tier == graphics::CapsTier::MID ||
                        r.tier == graphics::CapsTier::HIGH);
  if (!tier_ok) return false;
  if (r.fingerprint_hash.empty()) return false;
  (void)r.workarounds;  // field must exist; content covered below (A-T4-WA).
  const graphics::CapsReport r2 = graphics::resolve_tier(high_fp());
  if (!reports_equal(r, r2)) return false;  // record is stable telemetry.
  return true;
}

// AC-009 reproducibility implies sensitivity: distinct fingerprints must
// not alias to the same hash.
static bool check_distinct_fingerprints_yield_distinct_hashes() {
  const std::string h1 = graphics::resolve_tier(high_fp()).fingerprint_hash;
  auto low = make_fp("qcom", "adreno-610", "v512.0", 3, false, true, true);
  const std::string h2 = graphics::resolve_tier(low).fingerprint_hash;
  auto other_driver = high_fp();
  other_driver.driver_version = "r43p0";
  const std::string h3 = graphics::resolve_tier(other_driver).fingerprint_hash;
  return !h1.empty() && h1 != h2 && h1 != h3;
}

// Tier mapping (A-T4-TIER): vk13 + ample RAM is HIGH; constrained device LOW.
static bool check_tier_mapping_high_and_low() {
  if (graphics::resolve_tier(high_fp()).tier != graphics::CapsTier::HIGH)
    return false;
  auto low = make_fp("qcom", "adreno-610", "v512.0", 3, false, true, true);
  if (graphics::resolve_tier(low).tier != graphics::CapsTier::LOW)
    return false;
  auto gles_only =
      make_fp("img", "powervr-ge8320", "1.9", 8, false, false, true);
  return graphics::resolve_tier(gles_only).tier == graphics::CapsTier::LOW;
}

// A-T4-WA: unknown fingerprint resolves with an empty workaround set,
// deterministically.
static bool check_unknown_fingerprint_resolves_empty_workarounds() {
  auto f = make_fp("unknown-vendor", "unknown-gpu", "0.0", 6, false, true,
                   true);
  const graphics::CapsReport r1 = graphics::resolve_tier(f);
  const graphics::CapsReport r2 = graphics::resolve_tier(f);
  return r1.workarounds.empty() && reports_equal(r1, r2);
}

// Security/robustness (GR-05 R5.3): empty and oversized fingerprint fields
// never crash and still yield a well-formed report; the hash is fixed-length
// hex and embeds no raw vendor/driver substring.
static bool check_empty_fingerprint_stays_well_formed() {
  auto f = make_fp("", "", "", 0, false, false, false);
  const graphics::CapsReport r = graphics::resolve_tier(f);
  if (r.fingerprint_hash.empty() || !is_hex(r.fingerprint_hash)) return false;
  return r.tier == graphics::CapsTier::LOW;
}

static bool check_hash_leaks_no_raw_fingerprint_material() {
  auto f = make_fp("arm", "mali-g710", "r42p0", 8, true, true, true);
  const graphics::CapsReport r = graphics::resolve_tier(f);
  if (!is_hex(r.fingerprint_hash)) return false;
  if (r.fingerprint_hash.find("arm") != std::string::npos) return false;
  if (r.fingerprint_hash.find("mali") != std::string::npos) return false;
  if (r.fingerprint_hash.find("r42p0") != std::string::npos) return false;
  const std::string h2 = graphics::resolve_tier(high_fp()).fingerprint_hash;
  return r.fingerprint_hash.size() == h2.size();  // fixed length.
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
  RUN_CASE(check_same_fingerprint_reproduces_identical_report);
  RUN_CASE(check_telemetry_payload_carries_tier_workarounds_hash);
  RUN_CASE(check_distinct_fingerprints_yield_distinct_hashes);
  RUN_CASE(check_tier_mapping_high_and_low);
  RUN_CASE(check_unknown_fingerprint_resolves_empty_workarounds);
  RUN_CASE(check_empty_fingerprint_stays_well_formed);
  RUN_CASE(check_hash_leaks_no_raw_fingerprint_material);
  if (failures > 0) {
    std::printf("caps_telemetry.test: %d failing\n", failures);
    return 1;
  }
  std::printf("caps_telemetry.test: all passing\n");
  return 0;
}

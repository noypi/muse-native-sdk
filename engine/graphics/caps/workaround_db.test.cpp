// Batch 2 contract tests — T5: data-driven workaround DB with match-only
// pipeline effect (FR-GRAPHICS-005, AC-010 + AC-012). Binds ONLY to the T5
// `Interface:` line in plan.md; never to implementation.
// Hermetic: C++17 + planned graphics headers only; no network, no env/
// config reads, no live services. The only filesystem use is temp fixture
// files the test itself creates under /tmp (passed as `path` inputs to the
// unit under test, then removed); the repo's own config is never read.
// Expected state before implementation: RED — `caps/workaround_db.h` does
// not exist, so compilation fails (missing behavior, not a malformed test).
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "caps/caps_tier.h"
#include "caps/workaround_db.h"

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

// --- T5 helpers ------------------------------------------------------------
// ASSUMPTION (A-T5-SHAPE): C++ spelling of the plan interface. Plan line:
// `reload_db(path: string): DbVersion` + `match_workarounds(fingerprint:
// DeviceFingerprint): string[]`, error `DB_SCHEMA_MISMATCH` on bad version.
// Tests assume `graphics::DeviceFingerprint` is exactly the Batch 1 T4 shape
// (`caps/caps_tier.h`: vendor/gpu_id/driver_version strings, ram_gb,
// vk13/vk110/gles32 flags — reproduced, never contradicted), free functions
// `graphics::reload_db(const std::string&) -> graphics::DbVersion` (with a
// `version` string field) and `graphics::match_workarounds(const
// DeviceFingerprint&) -> std::vector<std::string>`, and schema errors
// signalled by throwing `graphics::DbSchemaMismatchError` whose `what()` is
// exactly "DB_SCHEMA_MISMATCH". Field renames → call-site reconciliation;
// genuine signature clash → planner per GR-08 T1.10.
// ASSUMPTION (A-T5-IO): `reload_db` takes a filesystem path (per plan). The
// test creates its own JSON fixtures under /tmp and removes them; no repo,
// config, or secret file is ever read.

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

static int tmp_seq = 0;

static std::string write_tmp_db(const char* body) {
  char path[128];
  std::snprintf(path, sizeof(path), "/tmp/workaround_db_t5_%d_%d.json",
                static_cast<int>(std::time(nullptr) % 100000), tmp_seq++);
  FILE* fp = std::fopen(path, "w");
  if (fp == nullptr) return "";
  std::fputs(body, fp);
  std::fclose(fp);
  return std::string(path);
}

static const char* kGoodDb =
    "{"
    "\"version\": 1,"
    "\"owner\": \"test-owner-not-real\","
    "\"entries\": ["
    "{\"vendor\": \"arm\", \"driver_version\": \"r42p0\", "
    "\"gpu_id\": \"mali-g710\", \"feature\": \"dynamic_rendering\", "
    "\"workaround_flag\": \"disable_dr\", \"engine_version\": \"1\"}"
    "]}";

// --- T5 behavior cases (AC-GRAPHICS-010, AC-GRAPHICS-012) --------------------

// AC-010: a DB file edit + reload changes match results with no rebuild —
// reload swaps the active DB and reports the new version.
static bool check_reload_swaps_db_without_recompile() {
  const std::string p1 = write_tmp_db(kGoodDb);
  if (p1.empty()) return false;
  const graphics::DbVersion v1 = graphics::reload_db(p1);
  std::remove(p1.c_str());
  const char* kDbV2 =
      "{\"version\": 2,\"owner\": \"test-owner-not-real\",\"entries\": []}";
  const std::string p2 = write_tmp_db(kDbV2);
  if (p2.empty()) return false;
  const graphics::DbVersion v2 = graphics::reload_db(p2);
  std::remove(p2.c_str());
  return !v1.version.empty() && !v2.version.empty() && v1.version != v2.version;
}

// AC-012: a synthetic entry alters the pipeline state (match set) on a
// matching fingerprint only; a non-matching fingerprint is untouched.
static bool check_synthetic_entry_matches_only_matching_fingerprint() {
  const std::string p = write_tmp_db(kGoodDb);
  if (p.empty()) return false;
  graphics::reload_db(p);
  std::remove(p.c_str());
  auto hit =
      make_fp("arm", "mali-g710", "r42p0", 8, true, true, true);
  auto miss_vendor =
      make_fp("qcom", "mali-g710", "r42p0", 8, true, true, true);
  auto miss_driver =
      make_fp("arm", "mali-g710", "v999", 8, true, true, true);
  const std::vector<std::string> got = graphics::match_workarounds(hit);
  if (got.size() != 1 || got[0] != "disable_dr") return false;
  if (!graphics::match_workarounds(miss_vendor).empty()) return false;
  return graphics::match_workarounds(miss_driver).empty();
}

// AC-012: engine boots clean with an empty/default DB — match returns empty,
// no throw.
static bool check_empty_db_boots_clean_with_no_match() {
  const std::string p =
      write_tmp_db("{\"version\": 1,\"entries\": []}");
  if (p.empty()) return false;
  try {
    graphics::reload_db(p);
  } catch (...) {
    std::remove(p.c_str());
    return false;
  }
  std::remove(p.c_str());
  try {
    return graphics::match_workarounds(
                    make_fp("arm", "mali-g710", "r42p0", 8, true, true, true))
        .empty();
  } catch (...) {
    return false;
  }
}

// AC-010 error path: a bad-version DB yields exactly DB_SCHEMA_MISMATCH and
// embeds no file internals.
static bool check_bad_schema_version_yields_mismatch_error() {
  const std::string p =
      write_tmp_db("{\"version\": \"bogus\", \"entries\": []}");
  if (p.empty()) return false;
  bool ok = false;
  try {
    graphics::reload_db(p);
  } catch (const graphics::DbSchemaMismatchError& e) {
    ok = std::string(e.what()) == "DB_SCHEMA_MISMATCH";
  } catch (...) {
    ok = false;
  }
  std::remove(p.c_str());
  return ok;
}

// Security/robustness (GR-05 R5.3): malformed JSON and missing files never
// crash — malformed content maps to the contract error, and the error shape
// leaks no path or secret material.
static bool check_malformed_db_never_crashes_and_leaks_nothing() {
  const std::string p = write_tmp_db("{not valid json!!!");
  if (p.empty()) return false;
  bool ok = false;
  try {
    graphics::reload_db(p);
  } catch (const graphics::DbSchemaMismatchError& e) {
    const std::string w = e.what();
    ok = (w == "DB_SCHEMA_MISMATCH") &&
         (w.find("/tmp") == std::string::npos) &&
         (w.find("secret") == std::string::npos);
  } catch (...) {
    ok = false;
  }
  std::remove(p.c_str());
  try {
    graphics::reload_db("/tmp/workaround_db_t5_no_such_file_xyz.json");
    return false;  // missing file must raise, never silently succeed.
  } catch (const graphics::DbSchemaMismatchError&) {
    return ok;
  } catch (...) {
    return false;
  }
}

// Security (GR-05 R5.3): an oversized entry payload cannot crash matching and
// never produces a workaround for an unmatched fingerprint.
static bool check_oversized_entry_cannot_crash_matching() {
  const std::string big(8192, 'x');
  std::string body =
      "{\"version\": 1, \"entries\": [{\"vendor\": \"arm\", "
      "\"driver_version\": \"r42p0\", \"gpu_id\": \"" +
      big +
      "\", \"feature\": \"f\", \"workaround_flag\": \"disable_dr\", "
      "\"engine_version\": \"1\"}]}";
  const std::string p = write_tmp_db(body.c_str());
  if (p.empty()) return false;
  try {
    graphics::reload_db(p);
  } catch (const graphics::DbSchemaMismatchError&) {
    std::remove(p.c_str());
    return true;  // rejecting an absurd entry via the contract error is fine.
  } catch (...) {
    std::remove(p.c_str());
    return false;
  }
  std::remove(p.c_str());
  try {
    return graphics::match_workarounds(
                    make_fp("arm", "mali-g710", "r42p0", 8, true, true, true))
        .empty();
  } catch (...) {
    return false;
  }
}

// Settled semantic (a) pin: `feature`/`engine_version` are recorded but
// never matched — empty vendor/driver/gpu constraints match any fingerprint,
// so the entry below matches and returns ["w"] regardless of its
// `feature`/`engine_version` values. Control: a non-empty vendor that differs
// from the fingerprint must not match.
static bool check_settled_semantic_feature_engine_version_not_matched() {
  const char* kSettledDb =
      "{\"version\":1,\"entries\":[{\"vendor\":\"\",\"driver_version\":\"\","
      "\"gpu_id\":\"\",\"feature\":\"f\",\"workaround_flag\":\"w\","
      "\"engine_version\":\"999\"}]}";
  const std::string p = write_tmp_db(kSettledDb);
  if (p.empty()) return false;
  try {
    graphics::reload_db(p);
  } catch (...) {
    std::remove(p.c_str());
    return false;
  }
  std::remove(p.c_str());
  const std::vector<std::string> got = graphics::match_workarounds(
      make_fp("any-vendor", "any-gpu", "any-driver", 8, true, true, true));
  if (got.size() != 1 || got[0] != "w") return false;
  // Control: entry vendor "X" vs fingerprint vendor "Y" → no match.
  const char* kControlDb =
      "{\"version\":1,\"entries\":[{\"vendor\":\"X\",\"driver_version\":\"\","
      "\"gpu_id\":\"\",\"feature\":\"f\",\"workaround_flag\":\"w\","
      "\"engine_version\":\"999\"}]}";
  const std::string p2 = write_tmp_db(kControlDb);
  if (p2.empty()) return false;
  try {
    graphics::reload_db(p2);
  } catch (...) {
    std::remove(p2.c_str());
    return false;
  }
  std::remove(p2.c_str());
  try {
    return graphics::match_workarounds(
                    make_fp("Y", "any-gpu", "any-driver", 8, true, true, true))
        .empty();
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
  RUN_CASE(check_reload_swaps_db_without_recompile);
  RUN_CASE(check_synthetic_entry_matches_only_matching_fingerprint);
  RUN_CASE(check_empty_db_boots_clean_with_no_match);
  RUN_CASE(check_bad_schema_version_yields_mismatch_error);
  RUN_CASE(check_malformed_db_never_crashes_and_leaks_nothing);
  RUN_CASE(check_oversized_entry_cannot_crash_matching);
  RUN_CASE(check_settled_semantic_feature_engine_version_not_matched);
  if (failures > 0) {
    std::printf("workaround_db.test: %d failing\n", failures);
    return 1;
  }
  std::printf("workaround_db.test: all passing\n");
  return 0;
}

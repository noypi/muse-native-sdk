// Batch 1 contract tests — FR-NAV-006 / AC-NAV-013 (TC-NAV-013 mapping for
// schema side) and AC-NAV-014 (validation-acceptance side).
// Hermetic: C++17 + navigation headers only; manifest fixtures are written
// by the test itself to unique temp files and removed afterwards. No
// network, no env/config reads, no live services. Consults no
// implementation; binds to the T1/T3 `Interface:` lines.
//
// SECTION OWNERSHIP (GR-08 T1.8): T1 owns the "manifest schema fixture"
// section; the "URI validation acceptance" section is published here in full
// now and is re-published (extended only) by batch 2, which owns T3.
// Shared helpers belong to neither section; keep them stable.
//
// ASSUMPTION (C++ spelling): the plan `Interface:` line is language-free
// (`loadManifest(path: string)`). The C++ spelling below is the test's
// proposal — `LoadManifest(const std::string&)` in
// `navigation/applinks_manifest.h` returning `LoadManifestResult{ok,
// manifest, error}` with `ManifestError{reason, detail}` and
// `ManifestErrorReason::{UNKNOWN_HOST, BAD_PATTERN, MISSING_FINGERPRINT}`.
// Field names (`hosts`, `pathPatterns`, `sha256CertFingerprints`,
// `autoVerify`, `packageName`) mirror the plan Detail; route-descriptor
// field names (`host`, `routeId`, `query`, `QueryParam{name, type,
// required}`) are reused from `navigation/trie.h`. If the coder names them
// differently, update call sites, not expectations.
// ASSUMPTION (manifest JSON shape): single JSON object with `hosts[]`,
// `schemes[]`, `routes[]` ({host, pathPattern, routeId, query[]}),
// `packageName`, `sha256CertFingerprints[]`, `autoVerify` (default true).
// ASSUMPTION (missing file): the plan names only three reasons; a missing
// file must still fail load (`ok == false`, non-empty `detail`) but the
// test asserts no specific reason.
// ASSUMPTION (validation spelling, T3 section): `ValidateUri(uri,
// manifest)` in `navigation/uri_validation.h` returning `ValidateResult`
// with `accepted`, `routeId`, `params`, `reason`, and a `navigate` flag
// that is false on every reject. Limits (uri <= 4KB, param <= 1KB,
// params <= 32) are per the plan Detail.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "navigation/applinks_manifest.h"
#include "navigation/trie.h"
#include "navigation/uri_validation.h"

namespace {

int failures = 0;
int tempCounter = 0;

#define EXPECT_TRUE(cond, name)                                     \
  do {                                                              \
    if (!(cond)) {                                                  \
      std::printf("FAIL %s\n", name);                               \
      ++failures;                                                   \
    } else {                                                        \
      std::printf("PASS %s\n", name);                               \
    }                                                               \
  } while (0)

std::string writeTempManifest(const std::string& body) {
  std::filesystem::path p =
      std::filesystem::temp_directory_path() /
      ("applinks_manifest_test_" + std::to_string(++tempCounter) + ".json");
  std::ofstream out(p);
  out << body;
  out.close();
  return p.string();
}

// AC-NAV-013 example: two hosts, one path each — loads clean.
const char* kTwoHostManifest = R"json({
  "hosts": ["shop.example.com", "social.example.com"],
  "schemes": ["https"],
  "packageName": "com.example.app",
  "sha256CertFingerprints": ["AA:BB:CC:DD"],
  "autoVerify": true,
  "routes": [
    {"host": "shop.example.com", "pathPattern": "/p/.*",
     "routeId": "product", "query": [{"name": "qty", "type": "int"}]},
    {"host": "social.example.com", "pathPattern": "/u/.*",
     "routeId": "profile", "query": []}
  ]
})json";

// ---- T1: manifest schema fixture section ----

bool check_two_hosts_load_clean() {
  std::string path = writeTempManifest(kTwoHostManifest);
  LoadManifestResult r = LoadManifest(path);
  std::remove(path.c_str());
  return r.ok && r.manifest.hosts.size() == 2 &&
         r.manifest.routes.size() == 2;
}

bool check_missing_fingerprints_fails_named_reason() {
  std::string body = R"json({
    "hosts": ["shop.example.com"],
    "schemes": ["https"],
    "packageName": "com.example.app",
    "sha256CertFingerprints": [],
    "routes": [
      {"host": "shop.example.com", "pathPattern": "/p/.*",
       "routeId": "product", "query": []}
    ]
  })json";
  std::string path = writeTempManifest(body);
  LoadManifestResult r = LoadManifest(path);
  std::remove(path.c_str());
  return !r.ok &&
         r.error.reason == ManifestErrorReason::MISSING_FINGERPRINT &&
         !r.error.detail.empty();
}

bool check_unknown_host_entry_fails_named_reason() {
  std::string body = R"json({
    "hosts": ["shop.example.com"],
    "schemes": ["https"],
    "packageName": "com.example.app",
    "sha256CertFingerprints": ["AA:BB:CC:DD"],
    "routes": [
      {"host": "evil.example.net", "pathPattern": "/p/.*",
       "routeId": "product", "query": []}
    ]
  })json";
  std::string path = writeTempManifest(body);
  LoadManifestResult r = LoadManifest(path);
  std::remove(path.c_str());
  return !r.ok && r.error.reason == ManifestErrorReason::UNKNOWN_HOST &&
         !r.error.detail.empty();
}

bool check_bad_pattern_fails_named_reason() {
  std::string body = R"json({
    "hosts": ["shop.example.com"],
    "schemes": ["https"],
    "packageName": "com.example.app",
    "sha256CertFingerprints": ["AA:BB:CC:DD"],
    "routes": [
      {"host": "shop.example.com", "pathPattern": "/p/([unclosed",
       "routeId": "product", "query": []}
    ]
  })json";
  std::string path = writeTempManifest(body);
  LoadManifestResult r = LoadManifest(path);
  std::remove(path.c_str());
  return !r.ok && r.error.reason == ManifestErrorReason::BAD_PATTERN &&
         !r.error.detail.empty();
}

bool check_error_detail_carries_field_path() {
  std::string body = R"json({
    "hosts": ["shop.example.com"],
    "schemes": ["https"],
    "packageName": "com.example.app",
    "sha256CertFingerprints": [],
    "routes": [
      {"host": "shop.example.com", "pathPattern": "/p/.*",
       "routeId": "product", "query": []}
    ]
  })json";
  std::string path = writeTempManifest(body);
  LoadManifestResult r = LoadManifest(path);
  std::remove(path.c_str());
  // The offending field path must appear in detail.
  return !r.ok && r.error.detail.find("sha256CertFingerprints") !=
                      std::string::npos;
}

bool check_autoverify_defaults_true() {
  std::string body = R"json({
    "hosts": ["shop.example.com"],
    "schemes": ["https"],
    "packageName": "com.example.app",
    "sha256CertFingerprints": ["AA:BB:CC:DD"],
    "routes": [
      {"host": "shop.example.com", "pathPattern": "/p/.*",
       "routeId": "product", "query": []}
    ]
  })json";
  std::string path = writeTempManifest(body);
  LoadManifestResult r = LoadManifest(path);
  std::remove(path.c_str());
  return r.ok && r.manifest.autoVerify;
}

bool check_missing_file_fails_load() {
  LoadManifestResult r = LoadManifest(
      "/nonexistent-dir-applinks/manifest_missing.json");
  return !r.ok && !r.error.detail.empty();
}

// ---- T3: URI validation acceptance section (owned by batch 2) ----

RouteManifest twoHostManifest() {
  std::string path = writeTempManifest(kTwoHostManifest);
  LoadManifestResult r = LoadManifest(path);
  std::remove(path.c_str());
  return r.manifest;
}

bool check_valid_link_accepts_with_typed_params() {
  RouteManifest m = twoHostManifest();
  ValidateResult r =
      ValidateUri("https://shop.example.com/p/42?qty=3", m);
  if (!r.accepted || r.navigate) return false;
  if (r.routeId != "product") return false;
  auto it = r.params.find("qty");
  return it != r.params.end() && it->second.type == ParamType::Int &&
         it->second.intValue == 3;
}

bool check_unknown_host_rejects_without_navigation() {
  RouteManifest m = twoHostManifest();
  ValidateResult r = ValidateUri("https://evil.example.net/p/1", m);
  return !r.accepted && !r.navigate &&
         r.reason == RejectReason::UNKNOWN_HOST;
}

bool check_path_mismatch_rejects_without_navigation() {
  RouteManifest m = twoHostManifest();
  ValidateResult r = ValidateUri("https://shop.example.com/nowhere", m);
  return !r.accepted && !r.navigate &&
         r.reason == RejectReason::PATH_MISMATCH;
}

bool check_bad_param_rejects() {
  RouteManifest m = twoHostManifest();
  ValidateResult r =
      ValidateUri("https://shop.example.com/p/7?qty=not-a-number", m);
  return !r.accepted && !r.navigate && r.reason == RejectReason::BAD_PARAM;
}

bool check_oversize_uri_rejects() {
  RouteManifest m = twoHostManifest();
  std::string big(5000, 'a');
  ValidateResult r =
      ValidateUri("https://shop.example.com/p/" + big, m);
  return !r.accepted && !r.navigate && r.reason == RejectReason::OVERSIZE;
}

bool check_embedded_credentials_reject() {
  RouteManifest m = twoHostManifest();
  ValidateResult r =
      ValidateUri("https://user:pass@shop.example.com/p/1", m);
  return !r.accepted && !r.navigate &&
         r.reason == RejectReason::CREDENTIALS;
}

bool check_encoded_traversal_rejects() {
  RouteManifest m = twoHostManifest();
  ValidateResult r =
      ValidateUri("https://shop.example.com/p/%2e%2e/secret", m);
  return !r.accepted && !r.navigate &&
         r.reason == RejectReason::TRAVERSAL;
}

bool check_wrong_scheme_rejects() {
  RouteManifest m = twoHostManifest();
  ValidateResult r =
      ValidateUri("http://shop.example.com/p/1", m);
  return !r.accepted && !r.navigate;
}

bool check_missing_required_param_rejects() {
  // "qty" is required on this self-built manifest; omitting it must be
  // BAD_PARAM (AC-NAV-014 typed-param check). A dedicated manifest is used
  // so the test does not depend on the shared fixture's required flags.
  const char* body = R"json({
    "hosts": ["shop.example.com"],
    "schemes": ["https"],
    "packageName": "com.example.app",
    "sha256CertFingerprints": ["AA:BB:CC:DD"],
    "routes": [
      {"host": "shop.example.com", "pathPattern": "/p/.*",
       "routeId": "product",
       "query": [{"name": "qty", "type": "int", "required": true}]}
    ]
  })json";
  std::string path = writeTempManifest(body);
  LoadManifestResult lr = LoadManifest(path);
  std::remove(path.c_str());
  if (!lr.ok) return false;
  ValidateResult r =
      ValidateUri("https://shop.example.com/p/7", lr.manifest);
  return !r.accepted && !r.navigate && r.reason == RejectReason::BAD_PARAM;
}

bool check_bool_param_accepts_typed() {
  RouteManifest m = twoHostManifest();
  // ASSUMPTION: manifest fixture below declares "debug" as Bool on the
  // profile route; the test builds its own manifest so it does not depend
  // on coder-chosen fixture content.
  const char* body = R"json({
    "hosts": ["social.example.com"],
    "schemes": ["https"],
    "packageName": "com.example.app",
    "sha256CertFingerprints": ["AA:BB:CC:DD"],
    "routes": [
      {"host": "social.example.com", "pathPattern": "/u/.*",
       "routeId": "profile",
       "query": [{"name": "admin", "type": "bool"}]}
    ]
  })json";
  std::string path = writeTempManifest(body);
  LoadManifestResult lr = LoadManifest(path);
  std::remove(path.c_str());
  if (!lr.ok) return false;
  ValidateResult r =
      ValidateUri("https://social.example.com/u/ada?admin=true", lr.manifest);
  if (!r.accepted || r.navigate) return false;
  auto it = r.params.find("admin");
  return it != r.params.end() && it->second.type == ParamType::Bool &&
         it->second.boolValue == true;
}

bool check_single_param_too_long_rejects() {
  RouteManifest m = twoHostManifest();
  // Single param value > 1KB must be OVERSIZE even when the URI is short.
  std::string bigValue(1100, 'x');
  ValidateResult r = ValidateUri(
      "https://shop.example.com/p/7?qty=3&pad=" + bigValue, m);
  return !r.accepted && !r.navigate && r.reason == RejectReason::OVERSIZE;
}

bool check_too_many_params_rejects() {
  RouteManifest m = twoHostManifest();
  // More than 32 params must be OVERSIZE.
  std::string uri = "https://shop.example.com/p/7?qty=3";
  for (int i = 0; i < 33; ++i) {
    uri += "&k" + std::to_string(i) + "=v";
  }
  ValidateResult r = ValidateUri(uri, m);
  return !r.accepted && !r.navigate && r.reason == RejectReason::OVERSIZE;
}

bool check_malformed_encoding_rejects() {
  RouteManifest m = twoHostManifest();
  ValidateResult r =
      ValidateUri("https://shop.example.com/p/%ZZ", m);
  return !r.accepted && !r.navigate && r.reason == RejectReason::ENCODING;
}

bool check_backslash_traversal_rejects() {
  RouteManifest m = twoHostManifest();
  ValidateResult r =
      ValidateUri("https://shop.example.com\\p\\secret", m);
  return !r.accepted && !r.navigate &&
         r.reason == RejectReason::TRAVERSAL;
}

bool check_dotdot_traversal_rejects() {
  RouteManifest m = twoHostManifest();
  ValidateResult r =
      ValidateUri("https://shop.example.com/p/../secret", m);
  return !r.accepted && !r.navigate &&
         r.reason == RejectReason::TRAVERSAL;
}

bool check_hostile_inputs_never_throw() {
  RouteManifest m = twoHostManifest();
  const char* hostile[] = {
      "https://evil.example.net/p/1",
      "https://user:pass@shop.example.com/p/1",
      "https://shop.example.com/p/%2e%2e/secret",
      "https://shop.example.com/p/%ZZ",
      "https://shop.example.com/p/../secret",
      "https://shop.example.com\\p\\secret",
  };
  try {
    for (const char* u : hostile) {
      ValidateResult r = ValidateUri(u, m);
      if (r.accepted) return false;
      if (r.navigate) return false;  // navigate:false invariant.
    }
  } catch (...) {
    return false;  // validateUri must never throw.
  }
  return true;
}

// Compile-time limit constants from the plan Detail (uri <= 4KB, single
// param <= 1KB, params <= 32) must exist in navigation/uri_validation.h.
static_assert(kMaxUriBytes == 4096, "uri limit must be 4KB");
static_assert(kMaxParamBytes == 1024, "param limit must be 1KB");
static_assert(kMaxParams == 32, "param count limit must be 32");

}  // namespace

int main() {
  // T1 fixture section.
  EXPECT_TRUE(check_two_hosts_load_clean(), "two_hosts_load_clean");
  EXPECT_TRUE(check_missing_fingerprints_fails_named_reason(),
              "missing_fingerprints_fails_named_reason");
  EXPECT_TRUE(check_unknown_host_entry_fails_named_reason(),
              "unknown_host_entry_fails_named_reason");
  EXPECT_TRUE(check_bad_pattern_fails_named_reason(),
              "bad_pattern_fails_named_reason");
  EXPECT_TRUE(check_error_detail_carries_field_path(),
              "error_detail_carries_field_path");
  EXPECT_TRUE(check_autoverify_defaults_true(), "autoverify_defaults_true");
  EXPECT_TRUE(check_missing_file_fails_load(), "missing_file_fails_load");
  // T3 validation section.
  EXPECT_TRUE(check_valid_link_accepts_with_typed_params(),
              "valid_link_accepts_with_typed_params");
  EXPECT_TRUE(check_unknown_host_rejects_without_navigation(),
              "unknown_host_rejects_without_navigation");
  EXPECT_TRUE(check_path_mismatch_rejects_without_navigation(),
              "path_mismatch_rejects_without_navigation");
  EXPECT_TRUE(check_bad_param_rejects(), "bad_param_rejects");
  EXPECT_TRUE(check_oversize_uri_rejects(), "oversize_uri_rejects");
  EXPECT_TRUE(check_embedded_credentials_reject(),
              "embedded_credentials_reject");
  EXPECT_TRUE(check_encoded_traversal_rejects(),
              "encoded_traversal_rejects");
  EXPECT_TRUE(check_wrong_scheme_rejects(), "wrong_scheme_rejects");
  EXPECT_TRUE(check_missing_required_param_rejects(),
              "missing_required_param_rejects");
  EXPECT_TRUE(check_bool_param_accepts_typed(),
              "bool_param_accepts_typed");
  EXPECT_TRUE(check_single_param_too_long_rejects(),
              "single_param_too_long_rejects");
  EXPECT_TRUE(check_too_many_params_rejects(), "too_many_params_rejects");
  EXPECT_TRUE(check_malformed_encoding_rejects(),
              "malformed_encoding_rejects");
  EXPECT_TRUE(check_backslash_traversal_rejects(),
              "backslash_traversal_rejects");
  EXPECT_TRUE(check_dotdot_traversal_rejects(),
              "dotdot_traversal_rejects");
  EXPECT_TRUE(check_hostile_inputs_never_throw(),
              "hostile_inputs_never_throw");
  if (failures > 0) {
    std::printf("uri_validation_test: %d failing\n", failures);
    return 1;
  }
  std::printf("uri_validation_test: all passing\n");
  return 0;
}

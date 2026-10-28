// Batch 2 contract tests — FR-NAV-006 / AC-NAV-015 (TC-NAV-015).
// Deferred-link capture -> persist -> first-launch replay (Referrer seam,
// restart-safe). Hermetic: C++17 + navigation headers only; the store path
// is injected per test into the OS temp directory and removed afterwards;
// the JNI seam `fetchInstallReferrer` is a test-local fake, never the real
// Play API. No network, no env/config reads, no live device/services.
// Consults no implementation; binds to the T4 `Interface:` line.
//
// ASSUMPTION (C++ spelling): the plan `Interface:` line is language-free
// (`captureReferrer(referrerUrl: string): void`,
// `drainPendingLaunch(): string | null`,
// `parseReferrer(installReferrer: string): string | null`, JNI seam
// `fetchInstallReferrer(): string`). The C++ spelling below is the test's
// proposal — `DeferredLinkStore` in `navigation/deferred_link.h` constructed
// with an app-private file path, `CaptureReferrer(const std::string&)`/
// `DrainPendingLaunch()` returning `std::optional<std::string>`, free
// function `ParseReferrer(const std::string&)` returning
// `std::optional<std::string>`, and injectable
// `FetchInstallReferrer()` fake. If the coder names them differently,
// update call sites, not expectations.
// ASSUMPTION (SVC-GMS referrer format): Play Install Referrer value is a
// URL-encoded query string; the link rides in the `gclid` param OR a
// `link`/`deep_link` param. `ParseReferrer` extracts the first present link
// param, URL-decoding it; absent link param or malformed input -> nullopt.
// ASSUMPTION (validation on drain): drain re-runs T3 `ValidateUri` against
// the T1 manifest; an invalid deferred URI drains to nullopt (landing
// fallback, never an error screen). Tests that need a manifest reuse the
// two-host fixture shape from `uri_validation_test.cpp`.
// ASSUMPTION (sync persist): capture is synchronous to disk before ack; the
// restart test proves it by destroying and recreating the store object on
// the same file with no flush call in between.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "navigation/applinks_manifest.h"
#include "navigation/deferred_link.h"
#include "navigation/uri_validation.h"

namespace {

int failures = 0;
int tempCounter = 0;

#define EXPECT_TRUE(cond, name)                     \
  do {                                              \
    if (!(cond)) {                                  \
      std::printf("FAIL %s\n", name);               \
      ++failures;                                   \
    } else {                                        \
      std::printf("PASS %s\n", name);               \
    }                                               \
  } while (0)

std::string tempStorePath() {
  std::filesystem::path p =
      std::filesystem::temp_directory_path() /
      ("deferred_link_test_" + std::to_string(++tempCounter) + ".store");
  return p.string();
}

// Test-local fake for the JNI seam: returns whatever the test arms it
// with. The real Play Install Referrer API is never called.
std::string gFakeReferrer;
std::string FetchInstallReferrer() { return gFakeReferrer; }

// Fixture: a valid deep link the T3 validator accepts.
const char* kValidLink = "https://shop.example.com/p/42?qty=3";

bool check_capture_drain_roundtrip_returns_link() {
  std::string path = tempStorePath();
  {
    DeferredLinkStore store(path);
    store.CaptureReferrer(kValidLink);
    std::optional<std::string> got = store.DrainPendingLaunch();
    if (!got.has_value() || *got != kValidLink) {
      std::remove(path.c_str());
      return false;
    }
  }
  std::remove(path.c_str());
  return true;
}

bool check_restart_between_capture_and_drain_survives() {
  std::string path = tempStorePath();
  {
    DeferredLinkStore store(path);
    store.CaptureReferrer(kValidLink);
    // No explicit flush: capture must already be durable here.
  }  // store destroyed = simulated process kill.
  DeferredLinkStore reopened(path);
  std::optional<std::string> got = reopened.DrainPendingLaunch();
  std::remove(path.c_str());
  return got.has_value() && *got == kValidLink;
}

bool check_second_drain_returns_null() {
  std::string path = tempStorePath();
  DeferredLinkStore store(path);
  store.CaptureReferrer(kValidLink);
  std::optional<std::string> first = store.DrainPendingLaunch();
  std::optional<std::string> second = store.DrainPendingLaunch();
  std::remove(path.c_str());
  return first.has_value() && !second.has_value();
}

bool check_drain_empty_returns_null() {
  std::string path = tempStorePath();
  DeferredLinkStore store(path);
  std::optional<std::string> got = store.DrainPendingLaunch();
  std::remove(path.c_str());
  return !got.has_value();
}

bool check_capture_is_idempotent() {
  std::string path = tempStorePath();
  DeferredLinkStore store(path);
  store.CaptureReferrer(kValidLink);
  store.CaptureReferrer(kValidLink);
  std::optional<std::string> first = store.DrainPendingLaunch();
  std::optional<std::string> second = store.DrainPendingLaunch();
  std::remove(path.c_str());
  return first.has_value() && *first == kValidLink && !second.has_value();
}

bool check_parse_referrer_extracts_link() {
  // SVC-GMS format: URL-encoded query string; link in `link` param.
  std::optional<std::string> got = ParseReferrer(
      "gclid=TEST_GCLID_NOT_REAL&link=https%3A%2F%2Fshop.example.com%2Fp%2F42");
  return got.has_value() && *got == "https://shop.example.com/p/42";
}

bool check_parse_referrer_malformed_returns_null() {
  // No link-bearing param and garbage input must drain to null, never throw.
  try {
    std::optional<std::string> a = ParseReferrer("utm_source=test-no-link");
    std::optional<std::string> b = ParseReferrer("%%%not-a-query%%%");
    std::optional<std::string> c = ParseReferrer("");
    if (a.has_value() || b.has_value() || c.has_value()) return false;
  } catch (...) {
    return false;  // parseReferrer must never throw.
  }
  return true;
}

bool check_referrer_seam_fake_flows_to_capture() {
  // The injected JNI seam feeds capture; the fake stands in for Play.
  gFakeReferrer = "link=https%3A%2F%2Fshop.example.com%2Fp%2F42";
  std::string path = tempStorePath();
  DeferredLinkStore store(path);
  std::optional<std::string> parsed = ParseReferrer(FetchInstallReferrer());
  gFakeReferrer.clear();
  if (!parsed.has_value()) {
    std::remove(path.c_str());
    return false;
  }
  store.CaptureReferrer(*parsed);
  std::optional<std::string> got = store.DrainPendingLaunch();
  std::remove(path.c_str());
  return got.has_value() && *got == "https://shop.example.com/p/42";
}

bool check_invalid_deferred_uri_dropped_to_landing() {
  // Drain re-runs T3 ValidateUri: a hostile deferred link must drain to
  // null (landing fallback), never dispatch and never an error screen.
  const char* hostile = "https://evil.example.net/p/1";
  std::string path = tempStorePath();
  DeferredLinkStore store(path);
  store.CaptureReferrer(hostile);
  std::optional<std::string> got = store.DrainPendingLaunch();
  std::remove(path.c_str());
  // No crash + landing fallback (null) is the whole assertion: the link
  // must not come back out for dispatch.
  return !got.has_value();
}

}  // namespace

int main() {
  EXPECT_TRUE(check_capture_drain_roundtrip_returns_link(),
              "capture_drain_roundtrip_returns_link");
  EXPECT_TRUE(check_restart_between_capture_and_drain_survives(),
              "restart_between_capture_and_drain_survives");
  EXPECT_TRUE(check_second_drain_returns_null(),
              "second_drain_returns_null");
  EXPECT_TRUE(check_drain_empty_returns_null(), "drain_empty_returns_null");
  EXPECT_TRUE(check_capture_is_idempotent(), "capture_is_idempotent");
  EXPECT_TRUE(check_parse_referrer_extracts_link(),
              "parse_referrer_extracts_link");
  EXPECT_TRUE(check_parse_referrer_malformed_returns_null(),
              "parse_referrer_malformed_returns_null");
  EXPECT_TRUE(check_referrer_seam_fake_flows_to_capture(),
              "referrer_seam_fake_flows_to_capture");
  EXPECT_TRUE(check_invalid_deferred_uri_dropped_to_landing(),
              "invalid_deferred_uri_dropped_to_landing");
  if (failures > 0) {
    std::printf("deferred_link_test: %d failing\n", failures);
    return 1;
  }
  std::printf("deferred_link_test: all passing\n");
  return 0;
}

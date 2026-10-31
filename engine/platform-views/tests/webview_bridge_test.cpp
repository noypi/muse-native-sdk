// T1 contract tests — FR-PVIEWS-011 / AC-PVIEWS-016 / FR-SECURITY-002.
// Binds ONLY to the plan T1 `Interface:` line; never to implementation.
// Hermetic: C++17 + pviews/webview_bridge.h only; stub Hybrid host (no device,
// no activity/window), no network, no env/config reads, no filesystem, no live
// services, no real credentials/tokens (GR-05 R5.3).
// ASSUMPTION (see unit-tests/T1.md): the header additionally declares
//   void registerBridgeMethod(const std::string& method, const CapabilitySet& required);
// and CapabilitySet is default-constructible and set-like (insert/count), so the
// tests can seed the explicit allowlist the AC requires.
#include <cstdio>
#include <string>

#include "pviews/webview_bridge.h"

namespace {

CapabilitySet emptyCaps() { return CapabilitySet{}; }

CapabilitySet fullCaps() {
  CapabilitySet c{};
  c.insert("SEC-SANDBOX:bridge.allow");
  return c;
}

bool errorIsDeniedFor(const BridgeResult& r, const std::string& method) {
  return !r.allowed && r.error == "DENIED:" + method;
}

// Case: registered method with satisfied capability succeeds.
bool check_registered_with_capability_succeeds() {
  registerBridgeMethod("bridge.echo", emptyCaps());
  const BridgeResult r = invokeBridge("bridge.echo", "hello", emptyCaps());
  return r.allowed && r.error.empty();
}

// Case: unregistered method fails closed with exact DENIED shape, never throws.
bool check_unregistered_denied_shape() {
  const BridgeResult r =
      invokeBridge("bridge.notRegistered", "hello", fullCaps());
  return errorIsDeniedFor(r, "bridge.notRegistered") && r.result.empty();
}

// Case: registered method invoked without the required capability is denied.
bool check_missing_capability_denied() {
  CapabilitySet need{};
  need.insert("SEC-SANDBOX:bridge.allow");
  registerBridgeMethod("bridge.secure", need);
  const BridgeResult r = invokeBridge("bridge.secure", "hello", emptyCaps());
  return errorIsDeniedFor(r, "bridge.secure");
}

// Case: deny-by-default — full capabilities do not open unregistered methods.
bool check_deny_by_default_with_full_caps() {
  const BridgeResult r =
      invokeBridge("bridge.unknownEvenWithCaps", "hello", fullCaps());
  return errorIsDeniedFor(r, "bridge.unknownEvenWithCaps");
}

// Case: empty method name is denied with "DENIED:" shape, never throws.
bool check_empty_method_denied() {
  const BridgeResult r = invokeBridge("", "hello", fullCaps());
  return errorIsDeniedFor(r, "");
}

// Case: injection payload against an unregistered method is denied and the
// denial carries the method identity only — no payload secret leaks into error.
bool check_injection_payload_denied_without_leak() {
  const std::string evil = "'; DROP TABLE cookies; -- <script>alert(1)</script>";
  const BridgeResult r = invokeBridge("bridge.evil", evil, fullCaps());
  return errorIsDeniedFor(r, "bridge.evil") &&
         r.error.find(evil) == std::string::npos;
}

// Case: invokeBridge never throws, even on hostile/oversized input.
bool check_never_throws() {
  try {
    invokeBridge("", "", emptyCaps());
    invokeBridge(std::string(1 << 16, 'x'), std::string(1 << 20, 'y'),
                 fullCaps());
    invokeBridge("bridge.\0hidden", std::string("\0\0", 2), emptyCaps());
    invokeBridge("bridge.notRegistered", "hello", fullCaps());
  } catch (...) {
    return false;
  }
  return true;
}

}  // namespace

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_registered_with_capability_succeeds);
  RUN_CASE(check_unregistered_denied_shape);
  RUN_CASE(check_missing_capability_denied);
  RUN_CASE(check_deny_by_default_with_full_caps);
  RUN_CASE(check_empty_method_denied);
  RUN_CASE(check_injection_payload_denied_without_leak);
  RUN_CASE(check_never_throws);
  if (failures > 0) {
    std::printf("webview_bridge_test: %d failing\n", failures);
    return 1;
  }
  std::printf("webview_bridge_test: all passing\n");
  return 0;
}

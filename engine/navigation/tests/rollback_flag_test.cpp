// Batch 2 tests — FR-NAV-013 / AC-NAV-037 (TC-NAV-037).
// Rollout flag gate, DEFAULT-ON per user amendment: `IsArbitraryRouteEnabled()`
// returns true by default; false only when rollback is explicitly requested
// (documented name MUSE_ARBITRARY_ROUTE_ENABLED, default ON). Default →
// gate open, arbitrary allowed; explicitly disabled → denied, prev/next
// preserved.
// Hermetic: C++17 + navigation headers only; no network, no config files,
// no filesystem, no live services. The only process state touched is the
// single env var under test, set/unset in-process and restored in teardown
// (never inherited, never leaked).
//
// Binds to the T4 `Interface:` line verbatim:
//   `bool IsArbitraryRouteEnabled()`
// declared in `navigation/router_policy.h` (new or extended).
//
// ASSUMPTION (rollback signal): explicitly requesting rollback means
// `MUSE_ARBITRARY_ROUTE_ENABLED=0`. Any other value (including "1" or
// unset) leaves the default-ON gate open. If the coder documents a wider
// falsy set, extend the table — but "0"-means-rollback is the contract.
// ASSUMPTION (prev/next preserved): prev/next paging is FR-SAMPLE-014-owned
// and unchanged here; from the C++ side "preserved" means the existing
// RouterBridge pop/back entries keep working while the gate is disabled.
// Wiring the gate into the navigate path is the coder's T4 production work.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "navigation/router_bridge.h"
#include "navigation/router_policy.h"

namespace {

int failures = 0;

#define EXPECT_TRUE(cond, name)                       \
  do {                                                \
    if (!(cond)) {                                    \
      std::printf("FAIL %s\n", name);                 \
      ++failures;                                     \
    } else {                                          \
      std::printf("PASS %s\n", name);                 \
    }                                                 \
  } while (0)

const char* kFlag = "MUSE_ARBITRARY_ROUTE_ENABLED";

// Save any pre-existing value so the test never leaks process state.
struct EnvGuard {
  bool had = false;
  std::string old;
  EnvGuard() {
    const char* v = std::getenv(kFlag);
    if (v != nullptr) {
      had = true;
      old = v;
    }
  }
  ~EnvGuard() {
    if (had) {
      ::setenv(kFlag, old.c_str(), 1);
    } else {
      ::unsetenv(kFlag);
    }
  }
};

// Default → gate open: with the flag absent, arbitrary is allowed.
bool check_default_gate_open() {
  ::unsetenv(kFlag);
  return IsArbitraryRouteEnabled() == true;
}

// Default → arbitrary navigate allowed while the gate is open.
bool check_default_arbitrary_allowed() {
  ::unsetenv(kFlag);
  if (!IsArbitraryRouteEnabled()) return false;
  RouterBridge b({"home", "details", "settings"});
  BridgeResult r = b.invoke("navigate", "{\"route\":\"settings\"}");
  return r.ok && r.error == BridgeError::NONE;
}

// Explicit rollback (MUSE_ARBITRARY_ROUTE_ENABLED=0) → gate denied.
bool check_explicit_zero_denied() {
  ::setenv(kFlag, "0", 1);
  return IsArbitraryRouteEnabled() == false;
}

// Explicit opt-in (MUSE_ARBITRARY_ROUTE_ENABLED=1) → gate open.
bool check_explicit_one_open() {
  ::setenv(kFlag, "1", 1);
  return IsArbitraryRouteEnabled() == true;
}

// Explicitly disabled → prev/next preserved: pop/back entries keep working
// while the gate reports denied.
bool check_disabled_prev_next_preserved() {
  ::setenv(kFlag, "0", 1);
  if (IsArbitraryRouteEnabled()) return false;
  RouterBridge b({"home", "details", "settings"});
  std::string homeSnap = b.snapshotForPersist();
  if (!b.invoke("navigate", "{\"route\":\"details\"}").ok) return false;
  if (!b.invoke("back", "{}").ok) return false;
  if (b.snapshotForPersist() != homeSnap) return false;
  if (!b.invoke("navigate", "{\"route\":\"details\"}").ok) return false;
  BridgeResult popped = b.invoke("pop", "{}");
  return popped.ok && popped.error == BridgeError::NONE;
}

}  // namespace

int main() {
  EnvGuard guard;
  EXPECT_TRUE(check_default_gate_open(), "default gate open (flag absent)");
  EXPECT_TRUE(check_default_arbitrary_allowed(),
              "default arbitrary navigate allowed");
  EXPECT_TRUE(check_explicit_zero_denied(),
              "explicit MUSE_ARBITRARY_ROUTE_ENABLED=0 denied");
  EXPECT_TRUE(check_explicit_one_open(),
              "explicit MUSE_ARBITRARY_ROUTE_ENABLED=1 open");
  EXPECT_TRUE(check_disabled_prev_next_preserved(),
              "disabled gate preserves prev/next (pop/back still work)");
  std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES %d\n", failures);
  return failures == 0 ? 0 : 1;
}

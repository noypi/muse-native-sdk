// T1 contract tests — FR-NAV-003 / AC-NAV-006 (TC-NAV-006).
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services.
// Binds to the plan T1 `Interface:` line:
//   `GuardVerdict evaluateGuard(const GuardRequest& req);
//    bool reRunOnRestore(const GuardRequest& req);`
// where `GuardRequest{routeId:string, args:map<string,TypedParam>,
// authenticated:bool, flags:map<string,bool>}` and
// `GuardVerdict{allowed:bool, redirect:string, error:GuardError}` with
// `GuardError{NONE, AUTH_REQUIRED, FLAG_OFF, ENTITLEMENT_DENIED}`.
//
// ASSUMPTION (rule table): the plan `Interface:` line names no
// rule-registration API. These cases assume the coder ships a
// test-seeded/default rule table where route "profile" requires
// authentication (redirect "home"), route "beta" is gated on
// flags["beta"], and route "pro" requires flags["entitled_pro"]
// (re-run on restore denies when revoked). Route names and flag keys
// are the test's proposal; the T1 contract records the reconciliation
// rule (update call sites, not expectations) if the coder names them
// differently. The deny-by-default unknown-route case needs no table.
// ASSUMPTION (TypedParam): reused from `navigation/trie.h`
// (`TypedParam { ParamType type; ... }`); tests only set `type` and the
// matching accessor.
#include <cstdio>
#include <map>
#include <string>

#include "navigation/route_guard.h"
#include "navigation/trie.h"

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

bool check_deny_unauthenticated_auth_route_redirect_home() {
  GuardRequest req;
  req.routeId = "profile";
  req.authenticated = false;
  GuardVerdict v = evaluateGuard(req);
  return !v.allowed && v.redirect == "home" &&
         v.error == GuardError::AUTH_REQUIRED;
}

bool check_allow_authenticated_auth_route() {
  GuardRequest req;
  req.routeId = "profile";
  req.authenticated = true;
  GuardVerdict v = evaluateGuard(req);
  return v.allowed && v.error == GuardError::NONE;
}

bool check_flag_off_denies() {
  GuardRequest req;
  req.routeId = "beta";
  req.authenticated = true;
  req.flags = {{"beta", false}};
  GuardVerdict v = evaluateGuard(req);
  return !v.allowed && v.error == GuardError::FLAG_OFF;
}

bool check_flag_on_allows() {
  GuardRequest req;
  req.routeId = "beta";
  req.authenticated = true;
  req.flags = {{"beta", true}};
  GuardVerdict v = evaluateGuard(req);
  return v.allowed && v.error == GuardError::NONE;
}

bool check_restore_revoked_entitlement_denies() {
  GuardRequest req;
  req.routeId = "pro";
  req.authenticated = true;
  req.flags = {{"entitled_pro", false}};
  // Re-run on a restored blob with revoked entitlement must deny.
  bool rerun = reRunOnRestore(req);
  GuardVerdict v = evaluateGuard(req);
  return rerun && !v.allowed && v.error == GuardError::ENTITLEMENT_DENIED;
}

bool check_restore_valid_entitlement_allows() {
  GuardRequest req;
  req.routeId = "pro";
  req.authenticated = true;
  req.flags = {{"entitled_pro", true}};
  bool rerun = reRunOnRestore(req);
  GuardVerdict v = evaluateGuard(req);
  return rerun && v.allowed && v.error == GuardError::NONE;
}

bool check_unknown_route_deny_by_default() {
  GuardRequest req;
  req.routeId = "nope-unknown-xyz";
  req.authenticated = true;
  req.flags = {{"beta", true}, {"entitled_pro", true}};
  GuardVerdict v = evaluateGuard(req);
  return !v.allowed;
}

}  // namespace

int main() {
  EXPECT_TRUE(check_deny_unauthenticated_auth_route_redirect_home(),
              "deny_unauthenticated_auth_route_redirect_home");
  EXPECT_TRUE(check_allow_authenticated_auth_route(),
              "allow_authenticated_auth_route");
  EXPECT_TRUE(check_flag_off_denies(), "flag_off_denies");
  EXPECT_TRUE(check_flag_on_allows(), "flag_on_allows");
  EXPECT_TRUE(check_restore_revoked_entitlement_denies(),
              "restore_revoked_entitlement_denies");
  EXPECT_TRUE(check_restore_valid_entitlement_allows(),
              "restore_valid_entitlement_allows");
  EXPECT_TRUE(check_unknown_route_deny_by_default(),
              "unknown_route_deny_by_default");
  if (failures > 0) {
    std::printf("route_guard_test: %d failing\n", failures);
    return 1;
  }
  std::printf("route_guard_test: all passing\n");
  return 0;
}

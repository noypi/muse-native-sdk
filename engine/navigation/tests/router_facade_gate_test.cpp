// T3 contract tests — FR-NAV-014 / AC-NAV-042 (TC-NAV-042).
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services. Consults no implementation;
// binds to the T3 `Interface:` line verbatim (see unit-tests/T3.md):
//   `RouterFacadeGate::commit(route: string, params: ParamMap): GateResult`
//   where `GateResult = {ok: true} | {ok: false,
//   code: 'unknown-route'|'bad-params'|'guard-denied'}`;
//   JS-allowed navigate the gate denies still fails closed.
//
// ASSUMPTION (header): the plan names no header path. These cases include
// `navigation/router_facade_gate.h`; update the include, not expectations,
// if the coder names the file differently.
// ASSUMPTION (ParamMap): `std::map<std::string, TypedParam>` from
// `navigation/trie.h` — the engine's existing typed-arg map.
// ASSUMPTION (GateResult shape): `struct GateResult { bool ok; GateCode
// code; }` with `GateCode{OK, UNKNOWN_ROUTE, BAD_PARAMS, GUARD_DENIED}` —
// the C++ spelling of the plan's `{ok, code}` union. Update member access,
// not expectations, if the coder spells it differently.
// ASSUMPTION (construction): the plan names no constructor. These cases
// build `explicit RouterFacadeGate(std::vector<RouteDescriptor>
// descriptors)` with `RouteDescriptor` from `navigation/typed_routes.h`
// (the manifest-codegen'd descriptor source), and wire the guard via
// `setGuardHook(GuardFn)` mirroring `RouterBridge` (`GuardFn =
// std::function<bool(const std::string& routeId)>` from
// `navigation/snapshot.h`). A null/unset guard allows every route,
// preserving pre-gate behavior.
// ASSUMPTION (descriptors): general typed params beyond the hardcoded
// `profile` case — fixture route "lens" requires string param "id"; route
// "profile" is guard-gated (denied when the hook returns false for it).
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "navigation/router_facade_gate.h"
#include "navigation/trie.h"
#include "navigation/typed_routes.h"

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

TypedParam StringParam(const std::string& v) {
  TypedParam p;
  p.type = ParamType::String;
  p.stringValue = v;
  return p;
}

TypedParam IntParam(long long v) {
  TypedParam p;
  p.type = ParamType::Int;
  p.intValue = v;
  return p;
}

std::vector<RouteDescriptor> Descriptors() {
  RouteDescriptor home;
  home.routeId = "home";
  RouteDescriptor lens;
  lens.routeId = "lens";
  ParamDecl id;
  id.name = "id";
  id.type = ParamType::String;
  id.required = true;
  lens.params.push_back(id);
  RouteDescriptor profile;
  profile.routeId = "profile";
  return {home, lens, profile};
}

bool check_commit_known_route_ok() {
  RouterFacadeGate gate(Descriptors());
  std::map<std::string, TypedParam> params;
  params["id"] = StringParam("a");
  GateResult r = gate.commit("lens", params);
  return r.ok && r.code == GateCode::OK;
}

bool check_commit_unknown_route_denied() {
  RouterFacadeGate gate(Descriptors());
  std::map<std::string, TypedParam> params;
  GateResult r = gate.commit("no_such_route", params);
  return !r.ok && r.code == GateCode::UNKNOWN_ROUTE;
}

bool check_commit_missing_required_param_denied() {
  RouterFacadeGate gate(Descriptors());
  std::map<std::string, TypedParam> params;  // "id" required, absent
  GateResult r = gate.commit("lens", params);
  return !r.ok && r.code == GateCode::BAD_PARAMS;
}

bool check_commit_wrong_typed_param_denied() {
  RouterFacadeGate gate(Descriptors());
  std::map<std::string, TypedParam> params;
  params["id"] = IntParam(7);  // "id" is a string param
  GateResult r = gate.commit("lens", params);
  return !r.ok && r.code == GateCode::BAD_PARAMS;
}

bool check_guard_denies_js_allowed_navigate_fails_closed() {
  // The JS facade allows "profile" (known route, no params required) but
  // the C++ gate denies it: fails closed with guard-denied.
  RouterFacadeGate gate(Descriptors());
  gate.setGuardHook([](const std::string& routeId) {
    return routeId != "profile";
  });
  std::map<std::string, TypedParam> params;
  GateResult r = gate.commit("profile", params);
  return !r.ok && r.code == GateCode::GUARD_DENIED;
}

bool check_guard_allows_commits_ok() {
  RouterFacadeGate gate(Descriptors());
  gate.setGuardHook([](const std::string&) { return true; });
  std::map<std::string, TypedParam> params;
  params["id"] = StringParam("a");
  GateResult r = gate.commit("lens", params);
  return r.ok && r.code == GateCode::OK;
}

bool check_unset_guard_allows_known_route() {
  RouterFacadeGate gate(Descriptors());
  std::map<std::string, TypedParam> params;
  GateResult r = gate.commit("home", params);
  return r.ok && r.code == GateCode::OK;
}

}  // namespace

int main() {
  EXPECT_TRUE(check_commit_known_route_ok(), "commit known route with valid params ok");
  EXPECT_TRUE(check_commit_unknown_route_denied(), "commit unknown route denied unknown-route");
  EXPECT_TRUE(check_commit_missing_required_param_denied(),
              "commit missing required param denied bad-params");
  EXPECT_TRUE(check_commit_wrong_typed_param_denied(),
              "commit wrong-typed param denied bad-params");
  EXPECT_TRUE(check_guard_denies_js_allowed_navigate_fails_closed(),
              "js-allowed navigate gate denies fails closed guard-denied");
  EXPECT_TRUE(check_guard_allows_commits_ok(), "guard allows commits ok");
  EXPECT_TRUE(check_unset_guard_allows_known_route(), "unset guard allows known route");
  std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES %d\n", failures);
  return failures == 0 ? 0 : 1;
}

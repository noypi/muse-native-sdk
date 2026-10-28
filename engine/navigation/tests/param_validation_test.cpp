// T2 contract tests — FR-NAV-007 / AC-NAV-017 (TC-NAV-017).
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services.
// Binds to the plan T2 `Interface:` line:
//   `ParamVerdict validateParams(const RouteDescriptor& desc,
//    const std::map<std::string,TypedParam>& args);`
// where `RouteDescriptor{routeId:string, params:vector<ParamDecl>}`,
// `ParamDecl{name:string, type:ParamType, required:bool}` and
// `ParamVerdict{ok:bool, error:ParamError, diagnostic:string}` with
// `ParamError{NONE, MISSING_REQUIRED, TYPE_MISMATCH, UNKNOWN_PARAM}`.
// `ParamType`/`TypedParam` are reused from `navigation/trie.h`; this
// file declares no competing enum.
// Every failure yields a stable diagnostic:
//   `routeId.param: expected <type>, got <actual|missing>`
//
// ASSUMPTION (type names): the plan does not pin the `<type>` token
// spelling. Tests expect lowercase names ("int", "double", "bool",
// "string"). If the coder emits a different spelling, update the
// expected strings, not the format.
// ASSUMPTION (unknown-param diagnostic): rejected with
// `routeId.param: expected <type>, got <actual>` where `<type>` is
// "none" (no declared type); the `UNKNOWN_PARAM` error is the binding
// assertion, the token is the proposal.
#include <cstdio>
#include <map>
#include <string>
#include <vector>

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

TypedParam makeString(const std::string& s) {
  TypedParam p;
  p.type = ParamType::String;
  p.stringValue = s;
  return p;
}

TypedParam makeInt(long long v) {
  TypedParam p;
  p.type = ParamType::Int;
  p.intValue = v;
  return p;
}

RouteDescriptor makeProfileDesc() {
  RouteDescriptor d;
  d.routeId = "profile";
  d.params = {{"userId", ParamType::String, true},
              {"tab", ParamType::Int, false}};
  return d;
}

bool check_valid_args_pass() {
  RouteDescriptor d = makeProfileDesc();
  std::map<std::string, TypedParam> args = {{"userId", makeString("ada")},
                                            {"tab", makeInt(2)}};
  ParamVerdict v = validateParams(d, args);
  return v.ok && v.error == ParamError::NONE;
}

bool check_missing_required_fails_deterministic() {
  RouteDescriptor d = makeProfileDesc();
  std::map<std::string, TypedParam> args = {{"tab", makeInt(2)}};
  ParamVerdict v = validateParams(d, args);
  return !v.ok && v.error == ParamError::MISSING_REQUIRED &&
         v.diagnostic == "profile.userId: expected string, got missing";
}

bool check_int_for_string_fails_deterministic() {
  RouteDescriptor d = makeProfileDesc();
  std::map<std::string, TypedParam> args = {{"userId", makeInt(7)}};
  ParamVerdict v = validateParams(d, args);
  return !v.ok && v.error == ParamError::TYPE_MISMATCH &&
         v.diagnostic == "profile.userId: expected string, got int";
}

bool check_optional_absent_passes() {
  RouteDescriptor d = makeProfileDesc();
  std::map<std::string, TypedParam> args = {{"userId", makeString("ada")}};
  ParamVerdict v = validateParams(d, args);
  return v.ok && v.error == ParamError::NONE;
}

bool check_unknown_param_rejected() {
  RouteDescriptor d = makeProfileDesc();
  std::map<std::string, TypedParam> args = {{"userId", makeString("ada")},
                                            {"zzz_nope", makeString("x")}};
  ParamVerdict v = validateParams(d, args);
  return !v.ok && v.error == ParamError::UNKNOWN_PARAM;
}

}  // namespace

int main() {
  EXPECT_TRUE(check_valid_args_pass(), "valid_args_pass");
  EXPECT_TRUE(check_missing_required_fails_deterministic(),
              "missing_required_fails_deterministic");
  EXPECT_TRUE(check_int_for_string_fails_deterministic(),
              "int_for_string_fails_deterministic");
  EXPECT_TRUE(check_optional_absent_passes(), "optional_absent_passes");
  EXPECT_TRUE(check_unknown_param_rejected(), "unknown_param_rejected");
  if (failures > 0) {
    std::printf("param_validation_test: %d failing\n", failures);
    return 1;
  }
  std::printf("param_validation_test: all passing\n");
  return 0;
}

// Batch 1 tests — FR-NAV-011 / AC-NAV-033, AC-NAV-034 (TC-NAV-033).
// Reused (not altered): FR-NAV-007 / AC-NAV-017 (param validation),
// FR-NAV-004 / AC-NAV-007 (unknown route -> home degrade).
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services. Derived from specs/ACs above,
// never from the implementation.
//
// ASSUMPTION (construction): the plan `Interface:` line names no
// constructor. Cases construct `explicit RouterBridge(
// std::vector<std::string> routes)` mirroring router_bridge_test.cpp.
// Update call sites, not expectations, if the coder names it differently.
// ASSUMPTION (param table): the plan does not pin which route requires
// params. Tests assume route "profile" declares required string param
// "userId" (mirroring param_validation_test.cpp's profile descriptor:
// required string userId + optional int tab). Missing "userId" or an
// int-typed "userId" must yield BAD_ARGS via the FR-NAV-007 path.
// ASSUMPTION (params envelope): method == "navigate" accepts
// `{"route":"<id>","params":{...}}`; a non-object "params" value is
// malformed and must yield BAD_ARGS.
// ASSUMPTION (unknown-route degrade): invoke returns
// {ok:false, error:NAV_ERROR} AND the graph degrades to home (snapshot
// equals a fresh home snapshot), never a crash — per FR-NAV-004 reuse
// decided at the call site contract quoted in the plan.
#include <cstdio>
#include <string>
#include <vector>

#include "navigation/router_bridge.h"

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

const std::vector<std::string> kRoutes = {
    "home", "search", "details", "settings", "profile", "checkout"};

bool contains(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}

// (a) AC-NAV-033: navigate to an arbitrary non-adjacent route commits,
// observable via snapshotForPersist.
bool check_arbitrary_nonadjacent_commits() {
  RouterBridge b(kRoutes);
  std::string before = b.snapshotForPersist();
  BridgeResult r =
      b.invoke("navigate", "{\"route\":\"settings\",\"params\":{}}");
  if (!r.ok || r.error != BridgeError::NONE) return false;
  std::string after = b.snapshotForPersist();
  return !before.empty() && !after.empty() && before != after &&
         contains(after, "settings");
}

// (d) AC-NAV-033/034: one invoke jumps >1 step (home -> checkout skips
// search/details/settings/profile); no forward-only +1 semantic.
bool check_single_invoke_jumps_more_than_one_step() {
  RouterBridge b(kRoutes);
  BridgeResult r =
      b.invoke("navigate", "{\"route\":\"checkout\",\"params\":{}}");
  if (!r.ok || r.error != BridgeError::NONE) return false;
  return contains(b.snapshotForPersist(), "checkout");
}

// (b) AC-NAV-033 + AC-NAV-017 reuse: valid params commit ok.
bool check_valid_params_commit_ok() {
  RouterBridge b(kRoutes);
  BridgeResult r = b.invoke(
      "navigate",
      "{\"route\":\"profile\",\"params\":{\"userId\":\"ada\"}}");
  if (!r.ok || r.error != BridgeError::NONE) return false;
  return contains(b.snapshotForPersist(), "profile");
}

// (b) AC-NAV-017 reuse: missing required param -> deterministic BAD_ARGS.
bool check_missing_required_param_denied() {
  RouterBridge b(kRoutes);
  BridgeResult r =
      b.invoke("navigate", "{\"route\":\"profile\",\"params\":{}}");
  return !r.ok && r.error == BridgeError::BAD_ARGS;
}

// (b) AC-NAV-017 reuse: ill-typed param (int for string userId) -> BAD_ARGS.
bool check_ill_typed_param_denied() {
  RouterBridge b(kRoutes);
  BridgeResult r = b.invoke(
      "navigate", "{\"route\":\"profile\",\"params\":{\"userId\":7}}");
  return !r.ok && r.error == BridgeError::BAD_ARGS;
}

// (b) Envelope shape: non-object params value -> BAD_ARGS.
bool check_non_object_params_denied() {
  RouterBridge b(kRoutes);
  BridgeResult r = b.invoke(
      "navigate", "{\"route\":\"details\",\"params\":\"oops\"}");
  return !r.ok && r.error == BridgeError::BAD_ARGS;
}

// (c) AC-NAV-007 reuse: unknown route -> NAV_ERROR and home degrade,
// never a crash. Push off home first so a no-op would NOT equal home.
bool check_unknown_route_nav_error_and_home_degrade() {
  RouterBridge b(kRoutes);
  if (!b.invoke("navigate", "{\"route\":\"details\",\"params\":{}}").ok)
    return false;
  BridgeResult r =
      b.invoke("navigate", "{\"route\":\"no_such_route\",\"params\":{}}");
  if (r.ok || r.error != BridgeError::NAV_ERROR) return false;
  RouterBridge fresh(kRoutes);
  return b.snapshotForPersist() == fresh.snapshotForPersist();
}

// (e) AC-NAV-033 envelope robustness (security-reviewer): an aliasing
// envelope whose nested params string smuggles `"route":"settings"`
// resolves to the top-level route home — or fails closed — and never
// commits settings.
bool check_aliasing_envelope_never_commits_smuggled_route() {
  RouterBridge b(kRoutes);
  BridgeResult r = b.invoke(
      "navigate",
      "{\"params\":{\"x\":\"\\\"route\\\":\\\"settings\\\"\"},"
      "\"route\":\"home\"}");
  std::string after = b.snapshotForPersist();
  if (contains(after, "settings")) return false;
  if (r.ok) {
    return r.error == BridgeError::NONE &&
           r.json == "{\"route\":\"home\"}" && contains(after, "home");
  }
  return r.error == BridgeError::BAD_ARGS;
}

// (f) AC-NAV-017 envelope robustness (security-reviewer, mirrors the
// JS-side array passthrough guard): array-literal params at the bridge
// is malformed and yields BAD_ARGS denial, never a commit.
bool check_array_literal_params_denied() {
  RouterBridge b(kRoutes);
  std::string before = b.snapshotForPersist();
  BridgeResult r = b.invoke(
      "navigate", "{\"route\":\"details\",\"params\":[1]}");
  return !r.ok && r.error == BridgeError::BAD_ARGS &&
         b.snapshotForPersist() == before;
}

// (g) Route echo safety (security-reviewer): the post-commit echo
// (`{"route":"<id>"}`) is only reached for routes in routes_, so a
// quote/backslash-bearing route id can never produce a malformed echo
// envelope — the commit is denied with no echo, and a legitimate commit
// echoes exactly one well-formed envelope.
bool check_malicious_route_id_produces_no_echo() {
  RouterBridge b(kRoutes);
  RouterBridge fresh(kRoutes);
  BridgeResult q = b.invoke(
      "navigate", "{\"route\":\"set\\\"tings\",\"params\":{}}");
  if (q.ok || q.error != BridgeError::NAV_ERROR || !q.json.empty())
    return false;
  BridgeResult s = b.invoke(
      "navigate", "{\"route\":\"home\\\\x\",\"params\":{}}");
  if (s.ok || s.error != BridgeError::NAV_ERROR || !s.json.empty())
    return false;
  if (b.snapshotForPersist() != fresh.snapshotForPersist()) return false;
  BridgeResult good =
      b.invoke("navigate", "{\"route\":\"home\",\"params\":{}}");
  return good.ok && good.error == BridgeError::NONE &&
         good.json == "{\"route\":\"home\"}";
}

}  // namespace

int main() {
  EXPECT_TRUE(check_arbitrary_nonadjacent_commits(),
              "arbitrary non-adjacent route commits observable via snapshot");
  EXPECT_TRUE(check_single_invoke_jumps_more_than_one_step(),
              "single invoke jumps more than one step (no +1 semantic)");
  EXPECT_TRUE(check_valid_params_commit_ok(), "valid params commit ok");
  EXPECT_TRUE(check_missing_required_param_denied(),
              "missing required param denied BAD_ARGS");
  EXPECT_TRUE(check_ill_typed_param_denied(),
              "ill-typed param denied BAD_ARGS");
  EXPECT_TRUE(check_non_object_params_denied(),
              "non-object params denied BAD_ARGS");
  EXPECT_TRUE(check_unknown_route_nav_error_and_home_degrade(),
              "unknown route NAV_ERROR and home degrade");
  EXPECT_TRUE(check_aliasing_envelope_never_commits_smuggled_route(),
              "aliasing envelope never commits smuggled route");
  EXPECT_TRUE(check_array_literal_params_denied(),
              "array-literal params denied BAD_ARGS");
  EXPECT_TRUE(check_malicious_route_id_produces_no_echo(),
              "malicious route id produces no echo; legit echo well-formed");
  std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES %d\n", failures);
  return failures == 0 ? 0 : 1;
}

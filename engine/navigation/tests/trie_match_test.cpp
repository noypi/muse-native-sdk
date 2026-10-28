// T2 contract tests — FR-NAV-005 / AC-NAV-009 (TC-NAV-009).
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services. Consults no implementation;
// binds to the T2 `Interface:` line.
//
// ASSUMPTION (manifest shape): the plan `Interface:` line names no
// construction/manifest API. These cases build the trie from a compiled
// route-manifest table passed at construction,
// `explicit RouteTrie(std::vector<RouteEntry> routes)`, where
// `RouteEntry { std::string host; std::vector<std::string> path;
// std::string routeId; std::vector<QueryParam> query; }`,
// `QueryParam { std::string name; ParamType type; bool required; }`, and
// `":name"` path segments capture parameters. Struct, enum and field
// names are the test's proposal; the T2 contract records the
// reconciliation rule (update call sites, not expectations) if the coder
// names them differently.
// ASSUMPTION (TypedParam shape): `TypedParam { ParamType type; ... }`
// carries the coerced value per its `type` (int/double/bool/string); tests
// assert `type` plus the matching accessor and never the union layout.
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "navigation/trie.h"

namespace {

int failures = 0;

#define EXPECT_TRUE(cond, name)                                     \
  do {                                                              \
    if (!(cond)) {                                                  \
      std::printf("FAIL %s\n", name);                               \
      ++failures;                                                   \
    } else {                                                        \
      std::printf("PASS %s\n", name);                               \
    }                                                               \
  } while (0)

RouteTrie makeTrie() {
  return RouteTrie({
      {"shop", {"p", ":id"}, "product",
       {{"qty", ParamType::Int, false}}},
      {"shop", {"c", ":slug"}, "collection", {}},
      {"social", {"u", ":user", "posts"}, "user_posts",
       {{"page", ParamType::Int, false},
        {"exact", ParamType::Bool, false}}},
      {"travel", {"book"}, "booking",
       {{"price", ParamType::Double, true},
        {"code", ParamType::String, true}}},
  });
}

bool check_exact_host_path_match() {
  RouteTrie t = makeTrie();
  MatchResult r = t.match("shop", {"c", "summer"}, {});
  return r.matched && r.routeId == "collection" && r.error == MatchError::NONE;
}

bool check_path_param_capture() {
  RouteTrie t = makeTrie();
  MatchResult r = t.match("shop", {"p", "42"}, {});
  if (!r.matched || r.routeId != "product") return false;
  auto it = r.args.find("id");
  return it != r.args.end() && it->second.type == ParamType::String;
}

bool check_typed_int_query_coercion() {
  RouteTrie t = makeTrie();
  MatchResult r = t.match("shop", {"p", "7"}, {{"qty", "3"}});
  if (!r.matched || r.routeId != "product") return false;
  auto it = r.args.find("qty");
  return it != r.args.end() && it->second.type == ParamType::Int &&
         it->second.intValue == 3;
}

bool check_typed_bool_query_coercion() {
  RouteTrie t = makeTrie();
  MatchResult r =
      t.match("social", {"u", "ada", "posts"}, {{"exact", "true"}});
  if (!r.matched || r.routeId != "user_posts") return false;
  auto it = r.args.find("exact");
  return it != r.args.end() && it->second.type == ParamType::Bool &&
         it->second.boolValue == true;
}

bool check_required_query_coercion() {
  RouteTrie t = makeTrie();
  MatchResult r = t.match("travel", {"book"},
                          {{"price", "19.5"}, {"code", "AB12"}});
  if (!r.matched || r.routeId != "booking") return false;
  auto p = r.args.find("price");
  auto c = r.args.find("code");
  return p != r.args.end() && p->second.type == ParamType::Double &&
         c != r.args.end() && c->second.type == ParamType::String;
}

bool check_no_match_returns_no_match() {
  RouteTrie t = makeTrie();
  MatchResult r = t.match("unknown", {"nowhere"}, {});
  return !r.matched && r.routeId.empty() && r.args.empty() &&
         r.error == MatchError::NO_MATCH;
}

bool check_wrong_host_is_no_match() {
  RouteTrie t = makeTrie();
  MatchResult r = t.match("shop", {"u", "ada", "posts"}, {});
  return !r.matched && r.error == MatchError::NO_MATCH;
}

bool check_type_mismatch_reports() {
  RouteTrie t = makeTrie();
  MatchResult r = t.match("shop", {"p", "7"}, {{"qty", "not-a-number"}});
  return !r.matched && r.error == MatchError::TYPE_MISMATCH;
}

bool check_missing_required_param_reports() {
  RouteTrie t = makeTrie();
  MatchResult r = t.match("travel", {"book"}, {{"code", "AB12"}});
  return !r.matched && r.error == MatchError::MISSING_PARAM;
}

bool check_third_host_nested_match() {
  RouteTrie t = makeTrie();
  MatchResult r = t.match("social", {"u", "grace", "posts"}, {});
  return r.matched && r.routeId == "user_posts" &&
         r.error == MatchError::NONE;
}

}  // namespace

int main() {
  EXPECT_TRUE(check_exact_host_path_match(), "exact_host_path_match");
  EXPECT_TRUE(check_path_param_capture(), "path_param_capture");
  EXPECT_TRUE(check_typed_int_query_coercion(), "typed_int_query_coercion");
  EXPECT_TRUE(check_typed_bool_query_coercion(), "typed_bool_query_coercion");
  EXPECT_TRUE(check_required_query_coercion(), "required_query_coercion");
  EXPECT_TRUE(check_no_match_returns_no_match(), "no_match_returns_no_match");
  EXPECT_TRUE(check_wrong_host_is_no_match(), "wrong_host_is_no_match");
  EXPECT_TRUE(check_type_mismatch_reports(), "type_mismatch_reports");
  EXPECT_TRUE(check_missing_required_param_reports(),
              "missing_required_param_reports");
  EXPECT_TRUE(check_third_host_nested_match(), "third_host_nested_match");
  if (failures > 0) {
    std::printf("trie_match_test: %d failing\n", failures);
    return 1;
  }
  std::printf("trie_match_test: all passing\n");
  return 0;
}

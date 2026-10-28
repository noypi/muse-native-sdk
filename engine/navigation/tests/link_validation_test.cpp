// T1 contract tests — NFR-NAV-002 / AC-NAV-002 (TC-NAV-002).
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services. Consults no implementation;
// binds to the T1 `Interface:` line.
//
// NOTE: `engine/navigation/include/navigation/link_validation.h` and
// `engine/navigation/src/link_validation.cpp` do not exist yet (T1 owns
// them). This file does NOT compile until T1 lands — that is the expected
// red state. CMake registration is owned by T2 and intentionally absent.
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "navigation/link_validation.h"
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

LinkAllowlist makeAllowlist() {
  LinkAllowlist allow;
  allow.schemes = {"https"};
  allow.routes = {
      {"shop", {"p", ":id"}, "product", {}},
      {"shop", {"c", ":slug"}, "collection", {}},
  };
  allow.maxParamBytes = 1024;
  allow.maxUriBytes = 4096;
  return allow;
}

LinkCandidate makeGood() {
  LinkCandidate in;
  in.scheme = "https";
  in.host = "shop";
  in.path = {"p", "42"};
  in.query = {};
  in.assetLinksVerified = true;
  return in;
}

bool check_verified_allowlisted_allowed() {
  LinkVerdict v = ValidateLink(makeGood(), makeAllowlist());
  return v.allowed && v.error == LinkError::NONE;
}

bool check_unverified_denied() {
  LinkCandidate in = makeGood();
  in.assetLinksVerified = false;
  LinkVerdict v = ValidateLink(in, makeAllowlist());
  return !v.allowed && v.error == LinkError::UNVERIFIED_HOST;
}

bool check_unknown_host_denied() {
  LinkCandidate in = makeGood();
  in.host = "evil";
  LinkVerdict v = ValidateLink(in, makeAllowlist());
  return !v.allowed && v.error == LinkError::UNKNOWN_ROUTE;
}

bool check_wrong_path_shape_denied() {
  LinkCandidate in = makeGood();
  in.path = {"p"};  // arity mismatch: route needs 2 segments
  LinkVerdict v = ValidateLink(in, makeAllowlist());
  // ASSUMPTION A2: unknown shape surfaces as UNKNOWN_ROUTE; a coder
  // returning SCHEMA_MISMATCH here is an equivalent deny — the binding
  // constraint is !allowed with a non-NONE error.
  return !v.allowed &&
         (v.error == LinkError::UNKNOWN_ROUTE ||
          v.error == LinkError::SCHEMA_MISMATCH);
}

bool check_wrong_scheme_denied() {
  LinkCandidate in = makeGood();
  in.scheme = "http";
  LinkVerdict v = ValidateLink(in, makeAllowlist());
  return !v.allowed && v.error == LinkError::SCHEMA_MISMATCH;
}

bool check_credentials_denied() {
  LinkCandidate in = makeGood();
  in.path = {"p", "user@evil"};
  LinkVerdict v = ValidateLink(in, makeAllowlist());
  return !v.allowed && v.error == LinkError::CREDENTIALS_PRESENT;
}

bool check_traversal_plain_denied() {
  LinkCandidate in = makeGood();
  in.path = {"p", ".."};
  LinkVerdict v = ValidateLink(in, makeAllowlist());
  return !v.allowed && v.error == LinkError::TRAVERSAL_DETECTED;
}

bool check_traversal_encoded_denied() {
  LinkCandidate in = makeGood();
  in.path = {"p", "%2e%2e%2fsecret"};
  LinkVerdict v = ValidateLink(in, makeAllowlist());
  return !v.allowed && v.error == LinkError::TRAVERSAL_DETECTED;
}

bool check_oversized_param_denied() {
  LinkCandidate in = makeGood();
  in.query = {{"q", std::string(2048, 'a')}};
  LinkVerdict v = ValidateLink(in, makeAllowlist());
  return !v.allowed && v.error == LinkError::OVERSIZED;
}

bool check_oversized_uri_denied() {
  LinkCandidate in = makeGood();
  in.path = {"p", std::string(5000, 'b')};
  LinkVerdict v = ValidateLink(in, makeAllowlist());
  return !v.allowed && v.error == LinkError::OVERSIZED;
}

bool check_empty_denied() {
  LinkCandidate in;
  in.scheme = "";
  in.host = "";
  in.path = {};
  in.query = {};
  in.assetLinksVerified = false;
  LinkVerdict v = ValidateLink(in, makeAllowlist());
  // ASSUMPTION A2: empty/garbage is a deny; the exact error enum is an
  // implementation choice (MALFORMED_URI expected) — binding is !allowed.
  return !v.allowed && v.error != LinkError::NONE;
}

bool check_garbage_denied() {
  LinkCandidate in;
  in.scheme = "ht+tp://[";
  in.host = "@@@";
  in.path = {"..", ".."};
  in.query = {{"\x01", "\xff"}};
  in.assetLinksVerified = false;
  LinkVerdict v = ValidateLink(in, makeAllowlist());
  return !v.allowed && v.error != LinkError::NONE;
}

bool check_revalidate_allowed_after_denies() {
  LinkAllowlist allow = makeAllowlist();
  LinkCandidate bad = makeGood();
  bad.assetLinksVerified = false;
  LinkVerdict d1 = ValidateLink(bad, allow);
  LinkCandidate evil = makeGood();
  evil.host = "evil";
  LinkVerdict d2 = ValidateLink(evil, allow);
  LinkVerdict v = ValidateLink(makeGood(), allow);
  // Denies navigate nowhere and commit nothing: a later valid link must
  // still validate clean, proving no deny-side state leaked.
  return !d1.allowed && !d2.allowed && v.allowed &&
         v.error == LinkError::NONE;
}

}  // namespace

int main() {
  EXPECT_TRUE(check_verified_allowlisted_allowed(),
              "verified_allowlisted_allowed");
  EXPECT_TRUE(check_unverified_denied(), "unverified_denied");
  EXPECT_TRUE(check_unknown_host_denied(), "unknown_host_denied");
  EXPECT_TRUE(check_wrong_path_shape_denied(), "wrong_path_shape_denied");
  EXPECT_TRUE(check_wrong_scheme_denied(), "wrong_scheme_denied");
  EXPECT_TRUE(check_credentials_denied(), "credentials_denied");
  EXPECT_TRUE(check_traversal_plain_denied(), "traversal_plain_denied");
  EXPECT_TRUE(check_traversal_encoded_denied(), "traversal_encoded_denied");
  EXPECT_TRUE(check_oversized_param_denied(), "oversized_param_denied");
  EXPECT_TRUE(check_oversized_uri_denied(), "oversized_uri_denied");
  EXPECT_TRUE(check_empty_denied(), "empty_denied");
  EXPECT_TRUE(check_garbage_denied(), "garbage_denied");
  EXPECT_TRUE(check_revalidate_allowed_after_denies(),
              "revalidate_allowed_after_denies");
  if (failures > 0) {
    std::printf("link_validation_test: %d failing\n", failures);
    return 1;
  }
  std::printf("link_validation_test: all passing\n");
  return 0;
}

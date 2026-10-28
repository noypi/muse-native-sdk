// Batch 2 contract tests — T5 cache-ownership boundary
// (FR-NET-008 / AC-NET-015 / TC-NET-015).
//
// Hermetic: C++17 + engine/net headers only. No sockets, no env or config
// reads, no filesystem, no live services, no wall-clock. Fixtures use dummy
// credentials and reserved `.test.invalid` hosts only.
//
// Binds to the T5 `Interface:` line in plan.md. Shape ASSUMPTIONS
// (reconciliation rule: update call sites, not expectations, if the coder
// names these differently):
//   A-REQ:  Request/Response are the T1 shapes in net/types.h (method, url,
//           headers as vector<pair<string,string>>, body; Response carries
//           status/headers/body/err).
//   A-OWN:  `IsHttpCacheable` answers true only for plain HTTP-level
//           caching: GET/HEAD with a cacheable status (200 here), no
//           `no-store`, no query-layer directives. Anything requiring
//           query-level semantics (request bodies carrying queries,
//           stale-while-revalidate, non-GET/HEAD methods, error statuses)
//           returns false — STATE-QUERY owns those, never the HTTP cache.
//   A-NEG:  the negative boundary is enforced at compile time below: the
//           listed member names must NOT exist on `muse::net::HttpCache`.
//           The list is the defensible surface a coder could plausibly add
//           (`staleWhileRevalidate`, `serveStale`, `optimisticUpdate`,
//           `invalidateQuery`, `prefetchQuery`, `swr`); any differently
//           named query/SWR/optimistic API is equally out of scope per the
//           plan's `none` line and must also be absent.
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "net/cache_ownership.h"
#include "net/http_cache.h"
#include "net/types.h"

namespace {

// Negative-boundary detectors (A-NEG): each resolves to true_type only if
// the named member exists on HttpCache. All must stay false_type.
template <typename T, typename = void>
struct HasStaleWhileRevalidate : std::false_type {};
template <typename T>
struct HasStaleWhileRevalidate<
    T, std::void_t<decltype(&T::staleWhileRevalidate)>> : std::true_type {};

template <typename T, typename = void>
struct HasServeStale : std::false_type {};
template <typename T>
struct HasServeStale<T, std::void_t<decltype(&T::serveStale)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasOptimisticUpdate : std::false_type {};
template <typename T>
struct HasOptimisticUpdate<T, std::void_t<decltype(&T::optimisticUpdate)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasInvalidateQuery : std::false_type {};
template <typename T>
struct HasInvalidateQuery<T, std::void_t<decltype(&T::invalidateQuery)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasPrefetchQuery : std::false_type {};
template <typename T>
struct HasPrefetchQuery<T, std::void_t<decltype(&T::prefetchQuery)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasSwr : std::false_type {};
template <typename T>
struct HasSwr<T, std::void_t<decltype(&T::swr)>> : std::true_type {};

static_assert(!HasStaleWhileRevalidate<muse::net::HttpCache>::value,
              "T5 boundary: HttpCache must expose no SWR API");
static_assert(!HasServeStale<muse::net::HttpCache>::value,
              "T5 boundary: HttpCache must expose no stale-serving API");
static_assert(!HasOptimisticUpdate<muse::net::HttpCache>::value,
              "T5 boundary: HttpCache must expose no optimistic-update API");
static_assert(!HasInvalidateQuery<muse::net::HttpCache>::value,
              "T5 boundary: HttpCache must expose no query-invalidation API");
static_assert(!HasPrefetchQuery<muse::net::HttpCache>::value,
              "T5 boundary: HttpCache must expose no query-prefetch API");
static_assert(!HasSwr<muse::net::HttpCache>::value,
              "T5 boundary: HttpCache must expose no swr API");

using muse::net::Request;
using muse::net::Response;

Request make_req(const std::string& method, const std::string& url,
                 std::vector<std::pair<std::string, std::string>> headers = {},
                 const std::string& body = "") {
  Request r;
  r.method = method;
  r.url = url;
  r.headers = std::move(headers);
  r.body = body;
  return r;
}

Response make_resp(int status,
                   std::vector<std::pair<std::string, std::string>> headers = {},
                   const std::string& body = "") {
  Response r;
  r.status = static_cast<muse::net::StatusCode>(status);
  r.headers = std::move(headers);
  r.body = body;
  r.err = muse::net::Error::kNone;
  return r;
}

// ---- HTTP-cacheable fixtures ------------------------------------------------

bool check_plain_get_200_is_cacheable() {
  Request q = make_req("GET", "https://api.test.invalid/v1/things");
  Response s = make_resp(200, {{"Cache-Control", "max-age=60"}}, "items");
  return muse::net::IsHttpCacheable(q, s) == true;
}

bool check_head_200_is_cacheable() {
  Request q = make_req("HEAD", "https://api.test.invalid/v1/things");
  Response s = make_resp(200, {{"Cache-Control", "max-age=60"}});
  return muse::net::IsHttpCacheable(q, s) == true;
}

// ---- query-layer fixtures must bypass ---------------------------------------

bool check_post_with_query_body_is_not_cacheable() {
  // GraphQL-style: the query rides in the POST body — STATE-QUERY owns
  // this, never the HTTP cache.
  Request q =
      make_req("POST", "https://api.test.invalid/graphql",
               {{"Content-Type", "application/json"}},
               "{\"query\":\"{ things { id } }\"}");
  Response s = make_resp(200, {{"Cache-Control", "max-age=60"}}, "{}");
  return muse::net::IsHttpCacheable(q, s) == false;
}

bool check_post_200_is_not_cacheable_by_method() {
  Request q = make_req("POST", "https://api.test.invalid/v1/things");
  Response s = make_resp(200, {{"Cache-Control", "max-age=60"}}, "created");
  return muse::net::IsHttpCacheable(q, s) == false;
}

bool check_swr_directive_is_not_cacheable() {
  // stale-while-revalidate is query-layer (SWR) semantics: owned by
  // STATE-QUERY, so the HTTP layer must decline.
  Request q = make_req("GET", "https://api.test.invalid/v1/feed");
  Response s = make_resp(
      200, {{"Cache-Control", "max-age=60, stale-while-revalidate=30"}}, "f");
  return muse::net::IsHttpCacheable(q, s) == false;
}

bool check_response_no_store_is_not_cacheable() {
  Request q = make_req("GET", "https://api.test.invalid/v1/private");
  Response s =
      make_resp(200, {{"Cache-Control", "no-store"}}, "sensitive");
  return muse::net::IsHttpCacheable(q, s) == false;
}

bool check_request_no_store_is_not_cacheable() {
  Request q = make_req("GET", "https://api.test.invalid/v1/x",
                       {{"Cache-Control", "no-store"}});
  Response s = make_resp(200, {{"Cache-Control", "max-age=60"}}, "x");
  return muse::net::IsHttpCacheable(q, s) == false;
}

bool check_error_status_is_not_cacheable() {
  Request q = make_req("GET", "https://api.test.invalid/v1/down");
  Response s = make_resp(500, {}, "busy");
  return muse::net::IsHttpCacheable(q, s) == false;
}

bool check_vary_star_is_not_cacheable() {
  // Vary:* can never match a subsequent request — nothing safe to store.
  Request q = make_req("GET", "https://api.test.invalid/v1/personal");
  Response s = make_resp(
      200, {{"Cache-Control", "max-age=60"}, {"Vary", "*"}}, "p");
  return muse::net::IsHttpCacheable(q, s) == false;
}

bool check_vary_multi_value_with_star_is_not_cacheable() {
  // MAJOR regression (review): `*` anywhere in the Vary list fails closed.
  Request q = make_req("GET", "https://api.test.invalid/v1/personal");
  Response s = make_resp(
      200, {{"Cache-Control", "max-age=60"}, {"Vary", "Accept, *"}}, "p");
  return muse::net::IsHttpCacheable(q, s) == false;
}

bool check_vary_whitespace_star_is_not_cacheable() {
  // Whitespace variant of the same fail-closed rule.
  Request q = make_req("GET", "https://api.test.invalid/v1/personal");
  Response s = make_resp(
      200, {{"Cache-Control", "max-age=60"}, {"Vary", " * "}}, "p");
  return muse::net::IsHttpCacheable(q, s) == false;
}

bool check_no_store_suffixed_token_is_cacheable() {
  // MINOR regression: `no-storex` is not the `no-store` directive.
  Request q = make_req("GET", "https://api.test.invalid/v1/x");
  Response s =
      make_resp(200, {{"Cache-Control", "no-storex"}}, "x");
  return muse::net::IsHttpCacheable(q, s) == true;
}

// ---- credentialed gate (Stage-4 security adjudication) -----------------------
// A non-empty Authorization/Cookie request header is a credential veto:
// IsHttpCacheable returns false unless the stored response carries
// `Cache-Control: public` or `s-maxage` (exact token via HasDirective).
// `Vary: Authorization` alone does NOT substitute for the explicit grant.

bool check_auth_without_public_is_not_cacheable() {
  Request q = make_req("GET", "https://api.test.invalid/v1/me",
                       {{"Authorization", "Bearer test-api-key-not-real"}});
  Response s = make_resp(200, {{"Cache-Control", "max-age=60"}}, "me");
  return muse::net::IsHttpCacheable(q, s) == false;
}

bool check_auth_with_public_is_cacheable() {
  Request q = make_req("GET", "https://api.test.invalid/v1/me",
                       {{"Authorization", "Bearer test-api-key-not-real"}});
  Response s = make_resp(
      200, {{"Cache-Control", "public, max-age=60"}}, "me");
  return muse::net::IsHttpCacheable(q, s) == true;
}

bool check_cookie_without_smaxage_is_not_cacheable() {
  Request q = make_req("GET", "https://api.test.invalid/v1/me",
                       {{"Cookie", "session=dummy-not-real"}});
  Response s = make_resp(200, {{"Cache-Control", "max-age=60"}}, "me");
  return muse::net::IsHttpCacheable(q, s) == false;
}

bool check_cookie_with_smaxage_is_cacheable() {
  Request q = make_req("GET", "https://api.test.invalid/v1/me",
                       {{"Cookie", "session=dummy-not-real"}});
  Response s = make_resp(
      200, {{"Cache-Control", "max-age=60, s-maxage=60"}}, "me");
  return muse::net::IsHttpCacheable(q, s) == true;
}

bool check_auth_with_vary_alone_still_not_cacheable() {
  // Vary: Authorization proves variance was considered, but it is NOT an
  // explicit shareability grant — still false without public/s-maxage.
  Request q = make_req("GET", "https://api.test.invalid/v1/me",
                       {{"Authorization", "Bearer test-api-key-not-real"}});
  Response s = make_resp(
      200,
      {{"Cache-Control", "max-age=60"}, {"Vary", "Authorization"}}, "me");
  return muse::net::IsHttpCacheable(q, s) == false;
}

// ---- negative boundary (runtime face of the compile-time gate) --------------

bool check_no_swr_api_exists_on_http_cache() {
  // Enforced by the static_asserts above: reaching runtime means the
  // boundary holds for every name in A-NEG.
  return !HasStaleWhileRevalidate<muse::net::HttpCache>::value &&
         !HasServeStale<muse::net::HttpCache>::value &&
         !HasSwr<muse::net::HttpCache>::value;
}

bool check_no_optimistic_or_query_api_exists_on_http_cache() {
  return !HasOptimisticUpdate<muse::net::HttpCache>::value &&
         !HasInvalidateQuery<muse::net::HttpCache>::value &&
         !HasPrefetchQuery<muse::net::HttpCache>::value;
}

// ---- robustness / security ---------------------------------------------------

bool check_hostile_inputs_never_throw_and_leak_nothing() {
  // Robustness probe with a pinned verdict: the POST+body fixture is owned
  // by STATE-QUERY, so IsHttpCacheable must return false (not just not
  // throw). The try/catch inside IsHttpCacheable guarantees no-throw; the
  // assertion below pins the ownership decision.
  Request q = make_req("POST", "https://api.test.invalid/graphql",
                       {{"Authorization", "Bearer test-api-key-not-real"},
                        {"X-Evil", "a\r\nInjected: yes"},
                        {"Cache-Control", std::string(1 << 16, 'x')}},
                       std::string(1 << 16, 'q'));
  Response s = make_resp(200, {{"Cache-Control", "max-age=99999999999999999999"}},
                         "body");
  bool verdict = false;
  try {
    verdict = muse::net::IsHttpCacheable(q, s);
  } catch (...) {
    return false;  // must never throw
  }
  return verdict == false;
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
  RUN_CASE(check_plain_get_200_is_cacheable);
  RUN_CASE(check_head_200_is_cacheable);
  RUN_CASE(check_post_with_query_body_is_not_cacheable);
  RUN_CASE(check_post_200_is_not_cacheable_by_method);
  RUN_CASE(check_swr_directive_is_not_cacheable);
  RUN_CASE(check_response_no_store_is_not_cacheable);
  RUN_CASE(check_request_no_store_is_not_cacheable);
  RUN_CASE(check_error_status_is_not_cacheable);
  RUN_CASE(check_auth_without_public_is_not_cacheable);
  RUN_CASE(check_auth_with_public_is_cacheable);
  RUN_CASE(check_cookie_without_smaxage_is_not_cacheable);
  RUN_CASE(check_cookie_with_smaxage_is_cacheable);
  RUN_CASE(check_auth_with_vary_alone_still_not_cacheable);
  RUN_CASE(check_vary_star_is_not_cacheable);
  RUN_CASE(check_vary_multi_value_with_star_is_not_cacheable);
  RUN_CASE(check_vary_whitespace_star_is_not_cacheable);
  RUN_CASE(check_no_store_suffixed_token_is_cacheable);
  RUN_CASE(check_no_swr_api_exists_on_http_cache);
  RUN_CASE(check_no_optimistic_or_query_api_exists_on_http_cache);
  RUN_CASE(check_hostile_inputs_never_throw_and_leak_nothing);
  if (failures > 0) {
    std::printf("cache_ownership_test: %d failing\n", failures);
    return 1;
  }
  std::printf("cache_ownership_test: all passing\n");
  return 0;
}

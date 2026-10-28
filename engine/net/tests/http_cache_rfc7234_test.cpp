// Batch 2 contract tests — T4 RFC 7234 HTTP cache
// (FR-NET-008 / AC-NET-013 / TC-NET-013).
//
// Hermetic: C++17 + engine/net headers only. The store is the coder-owned
// in-memory `MemoryCacheStore`; no sockets, no filesystem/mmap, no env or
// config reads, no live services, no wall-clock sleeps. Freshness is passed
// in directly (the coder owns tetra-state computation elsewhere); these
// tests pin the directive matrix the plan names: Cache-Control
// (max-age/no-store/no-cache), ETag/If-None-Match,
// Last-Modified/If-Modified-Since, Vary, Age.
//
// Binds to the T4 `Interface:` line in plan.md. Shape ASSUMPTIONS
// (reconciliation rule: update call sites, not expectations, if the coder
// names these differently):
//   A-REQ:  Request/Response are the T1 shapes in net/types.h (method, url,
//           headers as vector<pair<string,string>>, body; Response carries
//           status/headers/body/err).
//   A-ENTRY: `muse::net::CachedEntry` is default-constructible with an
//           assignable `response` (muse::net::Response) plus an assignable
//           `request` (muse::net::Request: the original request whose headers
//           produced this variant — selecting-field values for Vary
//           verification). Default-constructed when unknown → Vary
//           verification fails closed to Revalidate. Every other RFC 7234
//           signal (Cache-Control, ETag, Last-Modified, Age) travels inside
//           `response.headers` — no separate validator fields.
//           Vary rule (contract): parse field-names from the hit response
//           headers (case-insensitive, comma-split, trim; `Vary: *` always
//           mismatch → Revalidate); compare FirstValue(req.headers, field)
//           vs FirstValue(hit->request.headers, field) with exact/opaque
//           value compare (absent==absent match, present-vs-absent
//           mismatch); empty/whitespace-only Vary → no split (serve if
//           fresh). Never throws.
//   A-RESULT: `muse::net::ResponseOrRevalidate` exposes
//           `bool fromCache; Response response; Request revalidate;`.
//           `fromCache == true` means serve `response` with zero transport
//           calls; `false` means send `revalidate` (a conditional request
//           when validators exist, else the request unchanged).
//   A-FRESH: Freshness is caller-supplied (`kFresh`, `kStaleRevalidate`,
//           `kMustRevalidate`); the tests pass it directly.
//   A-KEY:  `CacheKey` covers method + full URL (query included). `Vary`
//           splitting is `handle()`'s job, not the key's.
//   A-HDR:  Header-name comparison is case-insensitive; tests use canonical
//           casing ("Cache-Control", "ETag", ...).
//   A-STORE: the coder ships `muse::net::MemoryCacheStore : public
//           ICacheStore` with a default constructor (in-memory map;
//           mmap backing explicitly deferred per plan A4).
//   A-SAFE: unparseable `max-age` is treated as stale (must revalidate),
//           never as fresh; `handle` never throws for any input.
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "net/http_cache.h"
#include "net/types.h"

namespace {

using muse::net::CachedEntry;
using muse::net::Freshness;
using muse::net::HttpCache;
using muse::net::ICacheStore;
using muse::net::MemoryCacheStore;
using muse::net::Request;
using muse::net::Response;

Request make_req(const std::string& method, const std::string& url,
                 std::vector<std::pair<std::string, std::string>> headers = {}) {
  Request r;
  r.method = method;
  r.url = url;
  r.headers = std::move(headers);
  r.body = "";
  return r;
}

Response make_resp(int status,
                   std::vector<std::pair<std::string, std::string>> headers,
                   const std::string& body) {
  Response r;
  r.status = static_cast<muse::net::StatusCode>(status);
  r.headers = std::move(headers);
  r.body = body;
  r.err = muse::net::Error::kNone;
  return r;
}

CachedEntry make_entry(
    Response resp,
    std::vector<std::pair<std::string, std::string>> stored_req_headers = {}) {
  CachedEntry e;
  e.response = std::move(resp);
  e.request.headers = std::move(stored_req_headers);
  return e;
}

char lower(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

std::string norm_name(const std::string& s) {
  std::string out = s;
  for (auto& c : out) c = lower(c);
  return out;
}

bool has_header_value(const std::vector<std::pair<std::string, std::string>>& hs,
                      const std::string& name, const std::string& want) {
  const std::string n = norm_name(name);
  for (const auto& h : hs) {
    if (norm_name(h.first) == n && h.second == want) return true;
  }
  return false;
}

bool has_header(const std::vector<std::pair<std::string, std::string>>& hs,
                const std::string& name) {
  const std::string n = norm_name(name);
  for (const auto& h : hs) {
    if (norm_name(h.first) == n) return true;
  }
  return false;
}

// ---- CacheKey -------------------------------------------------------------

bool check_cache_key_stable_for_same_get() {
  Request a = make_req("GET", "https://api.test.invalid/v1/things?page=1");
  Request b = make_req("GET", "https://api.test.invalid/v1/things?page=1");
  return muse::net::CacheKey(a) == muse::net::CacheKey(b) &&
         !muse::net::CacheKey(a).empty();
}

bool check_cache_key_splits_on_method() {
  Request g = make_req("GET", "https://api.test.invalid/v1/things");
  Request p = make_req("POST", "https://api.test.invalid/v1/things");
  return muse::net::CacheKey(g) != muse::net::CacheKey(p);
}

bool check_cache_key_splits_on_query() {
  Request a = make_req("GET", "https://api.test.invalid/v1/things?page=1");
  Request b = make_req("GET", "https://api.test.invalid/v1/things?page=2");
  return muse::net::CacheKey(a) != muse::net::CacheKey(b);
}

// ---- ICacheStore (in-memory) ----------------------------------------------

bool check_store_put_get_roundtrip() {
  MemoryCacheStore store;
  CachedEntry e =
      make_entry(make_resp(200, {{"ETag", "\"v1\""}}, "hello"));
  store.put("k1", e);
  std::optional<CachedEntry> got = store.get("k1");
  if (!got.has_value()) return false;
  return got->response.body == "hello" &&
         has_header_value(got->response.headers, "ETag", "\"v1\"");
}

bool check_store_get_missing_is_nullopt() {
  MemoryCacheStore store;
  return !store.get("never-stored").has_value();
}

// ---- handle(): freshness / validators -------------------------------------

bool check_fresh_hit_served_from_cache() {
  auto store = std::make_shared<MemoryCacheStore>();
  HttpCache cache(store);
  CachedEntry e = make_entry(make_resp(
      200, {{"Cache-Control", "max-age=60"}, {"Age", "5"}}, "fresh-body"));
  Request req = make_req("GET", "https://api.test.invalid/v1/fresh");
  auto out = cache.handle(req, &e, Freshness::kFresh);
  if (!out.fromCache) return false;
  if (out.response.body != "fresh-body") return false;
  // Age travels with the stored headers (TC-NET-013 Age half).
  return has_header_value(out.response.headers, "Age", "5");
}

bool check_stale_etag_sends_conditional() {
  auto store = std::make_shared<MemoryCacheStore>();
  HttpCache cache(store);
  CachedEntry e = make_entry(make_resp(
      200, {{"Cache-Control", "max-age=0"}, {"ETag", "\"abc\""}}, "stale"));
  Request req = make_req("GET", "https://api.test.invalid/v1/item");
  auto out = cache.handle(req, &e, Freshness::kStaleRevalidate);
  if (out.fromCache) return false;
  return has_header_value(out.revalidate.headers, "If-None-Match", "\"abc\"");
}

bool check_stale_last_modified_sends_conditional() {
  auto store = std::make_shared<MemoryCacheStore>();
  HttpCache cache(store);
  CachedEntry e = make_entry(make_resp(
      200,
      {{"Cache-Control", "max-age=0"},
       {"Last-Modified", "Wed, 28 Oct 2026 00:00:00 GMT"}},
      "stale"));
  Request req = make_req("GET", "https://api.test.invalid/v1/item");
  auto out = cache.handle(req, &e, Freshness::kStaleRevalidate);
  if (out.fromCache) return false;
  return has_header_value(out.revalidate.headers, "If-Modified-Since",
                          "Wed, 28 Oct 2026 00:00:00 GMT");
}

bool check_no_store_request_bypasses_fresh_hit() {
  auto store = std::make_shared<MemoryCacheStore>();
  HttpCache cache(store);
  CachedEntry e = make_entry(
      make_resp(200, {{"Cache-Control", "max-age=60"}}, "fresh-body"));
  Request req = make_req("GET", "https://api.test.invalid/v1/fresh",
                         {{"Cache-Control", "no-store"}});
  auto out = cache.handle(req, &e, Freshness::kFresh);
  return !out.fromCache;
}

bool check_no_cache_response_is_not_served_fresh() {
  auto store = std::make_shared<MemoryCacheStore>();
  HttpCache cache(store);
  CachedEntry e = make_entry(
      make_resp(200, {{"Cache-Control", "no-cache"}}, "must-check"));
  Request req = make_req("GET", "https://api.test.invalid/v1/nc");
  auto out = cache.handle(req, &e, Freshness::kFresh);
  return !out.fromCache;
}

bool check_must_revalidate_never_serves_stale() {
  auto store = std::make_shared<MemoryCacheStore>();
  HttpCache cache(store);
  CachedEntry e = make_entry(
      make_resp(200, {{"Cache-Control", "max-age=60"}}, "body"));
  Request req = make_req("GET", "https://api.test.invalid/v1/x");
  auto out = cache.handle(req, &e, Freshness::kMustRevalidate);
  return !out.fromCache;
}

bool check_null_hit_is_miss_with_unconditional_revalidate() {
  auto store = std::make_shared<MemoryCacheStore>();
  HttpCache cache(store);
  Request req = make_req("GET", "https://api.test.invalid/v1/absent");
  auto out = cache.handle(req, nullptr, Freshness::kFresh);
  if (out.fromCache) return false;
  // Miss: the revalidate request is the request itself (no validators to
  // attach); no conditional headers may be invented.
  if (out.revalidate.url != req.url || out.revalidate.method != req.method)
    return false;
  return !has_header(out.revalidate.headers, "If-None-Match") &&
         !has_header(out.revalidate.headers, "If-Modified-Since");
}

// ---- handle(): Vary --------------------------------------------------------

bool check_vary_mismatch_is_not_served() {
  auto store = std::make_shared<MemoryCacheStore>();
  HttpCache cache(store);
  CachedEntry e = make_entry(
      make_resp(200,
                {{"Cache-Control", "max-age=60"}, {"Vary", "Accept-Language"}},
                "en"),
      {{"Accept-Language", "en"}});
  Request req = make_req("GET", "https://api.test.invalid/v1/i18n",
                         {{"Accept-Language", "fr"}});
  // Stored variant was negotiated for a different value: even a fresh
  // entry must not serve across the Vary split.
  auto out = cache.handle(req, &e, Freshness::kFresh);
  return !out.fromCache;
}

bool check_vary_match_is_served() {
  auto store = std::make_shared<MemoryCacheStore>();
  HttpCache cache(store);
  CachedEntry e = make_entry(
      make_resp(200,
                {{"Cache-Control", "max-age=60"}, {"Vary", "Accept-Language"}},
                "en"),
      {{"Accept-Language", "en"}});
  Request req = make_req("GET", "https://api.test.invalid/v1/i18n",
                         {{"Accept-Language", "en"}});
  auto out = cache.handle(req, &e, Freshness::kFresh);
  return out.fromCache && out.response.body == "en";
}

bool check_vary_multi_field_match_is_served() {
  // Multi-field Vary: match on BOTH selecting fields → serve.
  auto store = std::make_shared<MemoryCacheStore>();
  HttpCache cache(store);
  CachedEntry e = make_entry(
      make_resp(200,
                {{"Cache-Control", "max-age=60"},
                 {"Vary", "Accept, Accept-Language"}},
                "variant"),
      {{"Accept", "text/html"}, {"Accept-Language", "en"}});
  Request req = make_req("GET", "https://api.test.invalid/v1/multi",
                         {{"Accept", "text/html"},
                          {"Accept-Language", "en"}});
  auto out = cache.handle(req, &e, Freshness::kFresh);
  return out.fromCache && out.response.body == "variant";
}

bool check_vary_multi_field_mismatch_revalidates() {
  // Multi-field Vary: mismatch on ONE of the fields → revalidate.
  auto store = std::make_shared<MemoryCacheStore>();
  HttpCache cache(store);
  CachedEntry e = make_entry(
      make_resp(200,
                {{"Cache-Control", "max-age=60"},
                 {"Vary", "Accept, Accept-Language"}},
                "variant"),
      {{"Accept", "text/html"}, {"Accept-Language", "en"}});
  Request req = make_req("GET", "https://api.test.invalid/v1/multi",
                         {{"Accept", "text/html"},
                          {"Accept-Language", "fr"}});
  auto out = cache.handle(req, &e, Freshness::kFresh);
  return !out.fromCache;
}

bool check_vary_star_revalidates_on_handle() {
  // Vary: * reaching handle() → always revalidate, even when fresh.
  auto store = std::make_shared<MemoryCacheStore>();
  HttpCache cache(store);
  CachedEntry e = make_entry(
      make_resp(200, {{"Cache-Control", "max-age=60"}, {"Vary", "*"}}, "p"),
      {{"Accept", "text/html"}});
  Request req = make_req("GET", "https://api.test.invalid/v1/star",
                         {{"Accept", "text/html"}});
  auto out = cache.handle(req, &e, Freshness::kFresh);
  return !out.fromCache;
}

// ---- robustness / security --------------------------------------------------

bool check_malformed_max_age_treated_as_stale_never_throws() {
  auto store = std::make_shared<MemoryCacheStore>();
  HttpCache cache(store);
  CachedEntry e = make_entry(make_resp(
      200, {{"Cache-Control", "max-age=not-a-number"}}, "body"));
  Request req = make_req("GET", "https://api.test.invalid/v1/x");
  auto out = cache.handle(req, &e, Freshness::kStaleRevalidate);
  // A-SAFE: unparseable max-age defaults to stale, never fresh, never throw.
  return !out.fromCache;
}

bool check_hostile_header_values_never_throw() {
  auto store = std::make_shared<MemoryCacheStore>();
  HttpCache cache(store);
  CachedEntry e = make_entry(make_resp(
      200,
      {{"Cache-Control", "max-age=60"},
       {"ETag", "\"a\r\nInjected: yes\""},
       {"Age", "99999999999999999999999"}},
      "body"));
  Request req = make_req("GET", "https://api.test.invalid/v1/x",
                         {{"Authorization", "Bearer test-api-key-not-real"},
                          {"If-None-Match", "*\r\nEvil: 1"}});
  auto out = cache.handle(req, &e, Freshness::kFresh);
  (void)out;  // any well-formed verdict is legal; the bar is no-throw
  auto out2 = cache.handle(req, &e, Freshness::kStaleRevalidate);
  (void)out2;
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
  RUN_CASE(check_cache_key_stable_for_same_get);
  RUN_CASE(check_cache_key_splits_on_method);
  RUN_CASE(check_cache_key_splits_on_query);
  RUN_CASE(check_store_put_get_roundtrip);
  RUN_CASE(check_store_get_missing_is_nullopt);
  RUN_CASE(check_fresh_hit_served_from_cache);
  RUN_CASE(check_stale_etag_sends_conditional);
  RUN_CASE(check_stale_last_modified_sends_conditional);
  RUN_CASE(check_no_store_request_bypasses_fresh_hit);
  RUN_CASE(check_no_cache_response_is_not_served_fresh);
  RUN_CASE(check_must_revalidate_never_serves_stale);
  RUN_CASE(check_null_hit_is_miss_with_unconditional_revalidate);
  RUN_CASE(check_vary_mismatch_is_not_served);
  RUN_CASE(check_vary_match_is_served);
  RUN_CASE(check_vary_multi_field_match_is_served);
  RUN_CASE(check_vary_multi_field_mismatch_revalidates);
  RUN_CASE(check_vary_star_revalidates_on_handle);
  RUN_CASE(check_malformed_max_age_treated_as_stale_never_throws);
  RUN_CASE(check_hostile_header_values_never_throw);
  if (failures > 0) {
    std::printf("http_cache_rfc7234_test: %d failing\n", failures);
    return 1;
  }
  std::printf("http_cache_rfc7234_test: all passing\n");
  return 0;
}

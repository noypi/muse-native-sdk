#pragma once

// HTTP-cache ownership boundary (FR-NET-008 / AC-NET-015 / TC-NET-015).
//
// IsHttpCacheable answers true only for plain HTTP-level caching: a
// GET/HEAD request paired with a cacheable (200) response, with no
// `no-store` on either side, no query-layer directives
// (stale-while-revalidate), no request body carrying query semantics,
// and no `Vary: *` (which can never match). Everything else returns
// false — STATE-QUERY owns those semantics, never the HTTP cache.
//
// Header-only and never throws: all inspection is bounds-checked string
// work over in-process fixtures; any exception maps to false.

#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "net/types.h"

namespace muse {
namespace net {

namespace detail {

// Lowercase an ASCII string without locale or allocation surprises.
inline std::string AsciiLower(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

inline std::string Trim(const std::string& s) {
  std::string::size_type b = 0;
  while (b < s.size() &&
         (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) {
    ++b;
  }
  std::string::size_type e = s.size();
  while (e > b &&
         (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' ||
          s[e - 1] == '\n')) {
    --e;
  }
  return s.substr(b, e - b);
}

// True when any header with the given (case-insensitive) name carries a
// Cache-Control directive satisfying `pred` on the lowercased,
// trimmed comma-token (parameters after '=' are kept in the token; name
// predicates strip them before comparing).
template <typename Pred>
bool HasCacheDirective(
    const std::vector<std::pair<std::string, std::string>>& headers,
    Pred pred) {
  for (const auto& h : headers) {
    if (AsciiLower(Trim(h.first)) != "cache-control") continue;
    const std::string value = AsciiLower(h.second);
    std::string::size_type pos = 0;
    while (pos <= value.size()) {
      std::string::size_type comma = value.find(',', pos);
      std::string token =
          Trim(value.substr(pos, comma == std::string::npos
                                       ? std::string::npos
                                       : comma - pos));
      // Strip directive parameters for the name comparison; predicates
      // match on the bare directive name so `name=value` forms are
      // covered too.
      if (!token.empty() && pred(token)) return true;
      if (comma == std::string::npos) break;
      pos = comma + 1;
    }
  }
  return false;
}

inline bool IsNoStoreToken(const std::string& token) {
  // Exact directive-name compare after stripping `=value` parameters
  // (mirrors HasDirective in src/http_cache.cc): `no-store="..."` matches,
  // `no-storex` / `no-storehouse` do not. Tokens arrive lowercased from
  // HasCacheDirective; the compare itself is case-sensitive by contract.
  std::string::size_type eq = token.find('=');
  return Trim(token.substr(0, eq)) == "no-store";
}

inline bool IsSwrToken(const std::string& token) {
  return token.rfind("stale-while-revalidate", 0) == 0;
}

inline bool IsPublicGrantToken(const std::string& token) {
  // Exact directive-name compare after stripping `=value` parameters
  // (mirrors IsNoStoreToken): `public` / `s-maxage=...` grant, `publicx`
  // does not. Tokens arrive lowercased from HasCacheDirective.
  std::string::size_type eq = token.find('=');
  std::string name = Trim(token.substr(0, eq));
  return name == "public" || name == "s-maxage";
}

// True when the request carries credential material: any non-empty
// (post-trim) Authorization or Cookie header value. Header name matched
// case-insensitively; empty value counts as absent, not credentialed.
inline bool HasCredentials(
    const std::vector<std::pair<std::string, std::string>>& headers) {
  for (const auto& h : headers) {
    std::string name = AsciiLower(Trim(h.first));
    if (name != "authorization" && name != "cookie") continue;
    if (!Trim(h.second).empty()) return true;
  }
  return false;
}

}  // namespace detail

inline bool IsHttpCacheable(const Request& r, const Response& resp) {
  try {
    // Only plain retrievable methods participate in the HTTP cache.
    if (r.method != "GET" && r.method != "HEAD") return false;
    // Query bodies (GraphQL-style POSTs, or any request carrying a body
    // that could encode query semantics) belong to STATE-QUERY.
    if (!r.body.empty()) return false;
    // Only a cacheable delivered status is storable here.
    if (resp.status != StatusCode::kOk) return false;
    if (resp.err != Error::kNone) return false;
    // Either side may veto storage with no-store.
    if (detail::HasCacheDirective(r.headers, detail::IsNoStoreToken)) {
      return false;
    }
    if (detail::HasCacheDirective(resp.headers, detail::IsNoStoreToken)) {
      return false;
    }
    // stale-while-revalidate is SWR (query-layer) semantics.
    if (detail::HasCacheDirective(resp.headers, detail::IsSwrToken)) {
      return false;
    }
    if (detail::HasCacheDirective(r.headers, detail::IsSwrToken)) {
      return false;
    }
    // Vary:* can never match a subsequent request — nothing safe to store.
    // Fail closed when ANY comma-separated field is `*` (mirrors
    // VaryMismatched in src/http_cache.cc): `Vary: "Accept, *"` and
    // `Vary: " * "` are uncacheable, not just the whole-value `"*"`.
    for (const auto& h : resp.headers) {
      if (detail::AsciiLower(detail::Trim(h.first)) != "vary") continue;
      const std::string& v = h.second;
      std::string::size_type pos = 0;
      while (pos <= v.size()) {
        std::string::size_type comma = v.find(',', pos);
        std::string field = detail::Trim(
            comma == std::string::npos ? v.substr(pos)
                                       : v.substr(pos, comma - pos));
        if (field == "*") return false;
        if (comma == std::string::npos) break;
        pos = comma + 1;
      }
    }
    // Credentialed veto: a request carrying non-empty Authorization/Cookie
    // material is never HTTP-cacheable unless the stored response carries
    // an explicit `Cache-Control: public` or `s-maxage` grant (exact token
    // via HasCacheDirective; `Vary: Authorization` does NOT substitute).
    if (detail::HasCredentials(r.headers) &&
        !detail::HasCacheDirective(resp.headers,
                                   detail::IsPublicGrantToken)) {
      return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace net
}  // namespace muse

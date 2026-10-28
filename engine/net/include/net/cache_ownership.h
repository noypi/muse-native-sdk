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
// trimmed comma-token (parameters after '=' are kept for prefix match).
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
      // Strip directive parameters for the name comparison only when the
      // predicate expects a bare name; predicates match on prefix so
      // `name=value` forms are covered too.
      if (!token.empty() && pred(token)) return true;
      if (comma == std::string::npos) break;
      pos = comma + 1;
    }
  }
  return false;
}

inline bool IsNoStoreToken(const std::string& token) {
  return token == "no-store" || token.rfind("no-store", 0) == 0;
}

inline bool IsSwrToken(const std::string& token) {
  return token.rfind("stale-while-revalidate", 0) == 0;
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
    for (const auto& h : resp.headers) {
      if (detail::AsciiLower(detail::Trim(h.first)) != "vary") continue;
      if (detail::Trim(h.second) == "*") return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace net
}  // namespace muse

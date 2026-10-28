#include "net/http_cache.h"

#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace muse {
namespace net {
namespace {

// Header-name comparison is case-insensitive (contract A-HDR). Values are
// copied opaquely and never interpreted, so hostile bytes (CRLF, huge Age)
// cannot escape as an exception or an injection.
bool NameEq(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    unsigned char ca = static_cast<unsigned char>(a[i]);
    unsigned char cb = static_cast<unsigned char>(b[i]);
    if (std::tolower(ca) != std::tolower(cb)) return false;
  }
  return true;
}

std::string Trim(const std::string& s) {
  std::size_t b = 0;
  while (b < s.size() &&
         (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n'))
    ++b;
  std::size_t e = s.size();
  while (e > b &&
         (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' ||
          s[e - 1] == '\n'))
    --e;
  return s.substr(b, e - b);
}

std::string Lower(std::string s) {
  for (auto& c : s) {
    c = static_cast<char>(
        std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

using Headers = std::vector<std::pair<std::string, std::string>>;

bool HasHeader(const Headers& hs, const std::string& name) {
  for (const auto& h : hs) {
    if (NameEq(h.first, name)) return true;
  }
  return false;
}

// First value for a header name, or empty when absent. Empty is also a
// legal value, so callers that need presence use HasHeader.
std::string FirstValue(const Headers& hs, const std::string& name) {
  for (const auto& h : hs) {
    if (NameEq(h.first, name)) return h.second;
  }
  return std::string();
}

// True when any Cache-Control header carries the directive token
// (case-insensitive, comma-separated, optional "=value").
bool HasDirective(const Headers& hs, const std::string& directive) {
  for (const auto& h : hs) {
    if (!NameEq(h.first, "cache-control")) continue;
    std::size_t pos = 0;
    const std::string& v = h.second;
    while (pos <= v.size()) {
      std::size_t comma = v.find(',', pos);
      std::string token = (comma == std::string::npos)
                              ? v.substr(pos)
                              : v.substr(pos, comma - pos);
      std::size_t eq = token.find('=');
      std::string name = Trim(token.substr(0, eq));
      if (Lower(name) == directive) return true;
      if (comma == std::string::npos) break;
      pos = comma + 1;
    }
  }
  return false;
}

// True when any max-age directive is present but not a plain non-negative
// integer (contract A-SAFE: unparseable max-age fails closed to stale).
bool MaxAgeMalformed(const Headers& hs) {
  for (const auto& h : hs) {
    if (!NameEq(h.first, "cache-control")) continue;
    std::size_t pos = 0;
    const std::string& v = h.second;
    while (pos <= v.size()) {
      std::size_t comma = v.find(',', pos);
      std::string token = (comma == std::string::npos)
                              ? v.substr(pos)
                              : v.substr(pos, comma - pos);
      std::size_t eq = token.find('=');
      if (eq != std::string::npos &&
          Lower(Trim(token.substr(0, eq))) == "max-age") {
        std::string val = Trim(token.substr(eq + 1));
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
          val = Trim(val.substr(1, val.size() - 2));
        if (val.empty()) return true;
        for (char c : val) {
          if (c < '0' || c > '9') return true;
        }
        // All digits: well-formed (overflow is irrelevant — a huge but
        // numeric max-age is still a number, so it is not "malformed").
      }
      if (comma == std::string::npos) break;
      pos = comma + 1;
    }
  }
  return false;
}

// True when the stored request is unknown (default-constructed): Vary
// verification fails closed to revalidation rather than risk serving the
// wrong variant on an absent==absent match.
bool StoredRequestUnknown(const Request& r) {
  return r.method.empty() && r.url.empty() && r.body.empty() &&
         r.headers.empty();
}

// True when the stored Vary split does not match the incoming request.
// Parses field-names from the hit response headers (case-insensitive,
// comma-split, trim; "Vary: *" always mismatch → Revalidate); compares
// FirstValue(req.headers, field) vs FirstValue(stored_req.headers, field)
// with exact/opaque value compare (absent==absent match,
// present-vs-absent mismatch); empty/whitespace-only Vary → no split.
bool VaryMismatched(const Headers& stored, const Request& req,
                    const Request& stored_req) {
  bool any_vary_header = false;
  bool has_fields = false;
  bool star = false;
  std::vector<std::string> fields;
  for (const auto& h : stored) {
    if (!NameEq(h.first, "vary")) continue;
    any_vary_header = true;
    std::size_t pos = 0;
    const std::string& v = h.second;
    while (pos <= v.size()) {
      std::size_t comma = v.find(',', pos);
      std::string field = Trim((comma == std::string::npos)
                                   ? v.substr(pos)
                                   : v.substr(pos, comma - pos));
      if (!field.empty()) {
        has_fields = true;
        if (field == "*")
          star = true;
        else
          fields.push_back(field);
      }
      if (comma == std::string::npos) break;
      pos = comma + 1;
    }
  }
  if (!any_vary_header) return false;
  if (star) return true;
  if (!has_fields) return false;  // empty/whitespace-only Vary: no split.
  if (StoredRequestUnknown(stored_req)) return true;
  for (const auto& field : fields) {
    bool req_has = HasHeader(req.headers, field);
    bool stored_has = HasHeader(stored_req.headers, field);
    if (req_has != stored_has) return true;
    if (req_has &&
        FirstValue(req.headers, field) !=
            FirstValue(stored_req.headers, field))
      return true;
  }
  return false;
}

// Miss verdict: send the request unchanged, inventing no validators.
ResponseOrRevalidate Miss(const Request& req) {
  ResponseOrRevalidate out;
  out.fromCache = false;
  out.revalidate = req;
  return out;
}

// Revalidation verdict: send the request plus stored validators (only the
// ones the request does not already carry), echoing the stale response for
// the caller's 304-merge step.
ResponseOrRevalidate Revalidate(const Request& req, const CachedEntry& hit) {
  ResponseOrRevalidate out;
  out.fromCache = false;
  out.response = hit.response;
  out.revalidate = req;
  std::string etag = FirstValue(hit.response.headers, "etag");
  if (!etag.empty() && !HasHeader(out.revalidate.headers, "if-none-match")) {
    out.revalidate.headers.emplace_back("If-None-Match", etag);
  }
  std::string last_mod =
      FirstValue(hit.response.headers, "last-modified");
  if (!last_mod.empty() &&
      !HasHeader(out.revalidate.headers, "if-modified-since")) {
    out.revalidate.headers.emplace_back("If-Modified-Since", last_mod);
  }
  return out;
}

}  // namespace

void MemoryCacheStore::put(const std::string& key, const CachedEntry& e) {
  map_[key] = e;
}

std::optional<CachedEntry> MemoryCacheStore::get(const std::string& key) {
  auto it = map_.find(key);
  if (it == map_.end()) return std::nullopt;
  return it->second;
}

HttpCache::HttpCache(std::shared_ptr<ICacheStore> s) : store_(std::move(s)) {}

ResponseOrRevalidate HttpCache::handle(const Request& req,
                                       const CachedEntry* hit, Freshness f) {
  try {
    // No entry: unconditional request, no invented validators.
    if (hit == nullptr) return Miss(req);
    const Headers& stored = hit->response.headers;
    // Request bypass directives (RFC 7234 5.2.1.4/5.2.1.5).
    if (HasDirective(req.headers, "no-store") ||
        HasDirective(req.headers, "no-cache")) {
      return Miss(req);
    }
    // Stored bypass directives: never serve without revalidation.
    if (HasDirective(stored, "no-store") ||
        HasDirective(stored, "no-cache")) {
      return Revalidate(req, *hit);
    }
    // Vary split verified against the stored request headers.
    if (VaryMismatched(stored, req, hit->request)) return Revalidate(req, *hit);
    // Caller-supplied freshness (contract A-FRESH).
    if (f != Freshness::kFresh) return Revalidate(req, *hit);
    // Malformed max-age is stale, never fresh (contract A-SAFE).
    if (MaxAgeMalformed(stored)) return Revalidate(req, *hit);
    // Fresh hit: serve the stored response verbatim (Age travels with the
    // stored headers) with zero transport calls.
    ResponseOrRevalidate out;
    out.fromCache = true;
    out.response = hit->response;
    out.revalidate = req;
    return out;
  } catch (...) {
    // handle() never throws for any input (contract A-SAFE): fail closed
    // to an unconditional request.
    ResponseOrRevalidate out;
    out.fromCache = false;
    try {
      out.revalidate = req;
    } catch (...) {
    }
    return out;
  }
}

std::string CacheKey(const Request& r) { return r.method + "\n" + r.url; }

}  // namespace net
}  // namespace muse

#pragma once

// RFC 7234 response-cache vocabulary (FR-NET-008 / AC-NET-013 / TC-NET-013).
//
// Freshness is caller-supplied (plan A-FRESH): this TU owns the directive
// matrix, not clock math. Every RFC 7234 signal (Cache-Control, ETag,
// Last-Modified, Age, Vary) travels inside response/request headers —
// CachedEntry carries no separate validator fields (contract A-ENTRY).

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "net/types.h"

namespace muse {
namespace net {

// Stored cache entry (contract A-ENTRY): default-constructible with an
// assignable response plus the assignable original request whose headers
// produced this variant (selecting-field values for Vary verification).
// Default-constructed when unknown → Vary verification fails closed to
// Revalidate. Validators/directives live in response.headers.
struct CachedEntry {
  Response response;
  Request request;
};

// handle() verdict (contract A-RESULT): fromCache == true means serve
// response with zero transport calls; false means send revalidate (a
// conditional request when validators exist, else the request unchanged).
struct ResponseOrRevalidate {
  bool fromCache = false;
  Response response;
  Request revalidate;
};

// Caller-supplied freshness (contract A-FRESH).
enum class Freshness {
  kFresh,
  kStaleRevalidate,
  kMustRevalidate,
};

// Cache store seam (plan T4 Interface line, verbatim). The in-memory
// MemoryCacheStore below is the store for this slice; mmap disk backing
// is explicitly deferred (plan A4).
class ICacheStore {
 public:
  virtual ~ICacheStore() = default;
  virtual void put(const std::string& key, const CachedEntry& e) = 0;
  virtual std::optional<CachedEntry> get(const std::string& key) = 0;
};

// In-memory ICacheStore (contract A-STORE).
class MemoryCacheStore : public ICacheStore {
 public:
  MemoryCacheStore() = default;
  void put(const std::string& key, const CachedEntry& e) override;
  std::optional<CachedEntry> get(const std::string& key) override;

 private:
  std::unordered_map<std::string, CachedEntry> map_;
};

// RFC 7234 directive matrix (plan T4 Interface line, verbatim).
class HttpCache {
 public:
  explicit HttpCache(std::shared_ptr<ICacheStore> s);
  ResponseOrRevalidate handle(const Request& req, const CachedEntry* hit,
                              Freshness f);

 private:
  std::shared_ptr<ICacheStore> store_;
};

// Cache key (contract A-KEY): method + full URL (query included). Vary
// splitting is handle()'s job, not the key's.
std::string CacheKey(const Request& r);

}  // namespace net
}  // namespace muse

#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include "muse/state/atom.hpp"

namespace muse {
namespace state {

// String-like key identifying a single query.
class QueryKey {
 public:
  QueryKey() = default;
  QueryKey(const char* key) : key_(key) {}  // NOLINTNEXTLINE(google-explicit-constructor)
  explicit QueryKey(std::string key) : key_(std::move(key)) {}

  const std::string& str() const { return key_; }

  bool operator==(const QueryKey& other) const { return key_ == other.key_; }
  bool operator!=(const QueryKey& other) const { return !(*this == other); }

 private:
  std::string key_;
};

// String-like tag grouping queries for invalidation.
class Tag {
 public:
  Tag() = default;
  Tag(const char* tag) : tag_(tag) {}  // NOLINTNEXTLINE(google-explicit-constructor)
  explicit Tag(std::string tag) : tag_(std::move(tag)) {}

  const std::string& str() const { return tag_; }

  bool operator==(const Tag& other) const { return tag_ == other.tag_; }
  bool operator!=(const Tag& other) const { return !(*this == other); }

 private:
  std::string tag_;
};

using Value = TypedValue;
using Generation = std::uint64_t;

enum class IdempotencyHint {
  kIdempotent = 0,
  kNonIdempotent = 1,
};

struct Response {
  Value value;
  Generation generation = 0;
};

// FR-NET-001/NET-CACHE seam (fake in tests). The `idempotency()` accessor lets
// later items (T6 non-idempotent rule) observe the hint through the seam.
class Transport {
 public:
  virtual ~Transport() = default;
  virtual Response get(QueryKey k, IdempotencyHint h) = 0;
  virtual IdempotencyHint idempotency() const = 0;
};

// Immediately-ready future: fetch resolves synchronously once the (single)
// coalesced transport call returns, so get() yields a StatusOr<Value>.
template <typename T>
class Future {
 public:
  explicit Future(StatusOr<T> result) : result_(std::move(result)) {}

  StatusOr<T> get() const { return result_; }

 private:
  StatusOr<T> result_;
};

// Invalidation-driven write-through cache copy (Q-STATE-002): the cache never
// writes back to the transport. Concurrent fetch() calls on one key issue a
// single Transport::get and fan the result out to all observers.
class QueryClient {
 public:
  explicit QueryClient(Transport* t);

  Future<Value> fetch(QueryKey k, ObserverId obs);
  Status invalidateQueries(Tag t);
  Status setQueryData(QueryKey k, Value v);
  Status cancelQueries(QueryKey k);

 private:
  Transport* transport_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, Value> cache_;
};

}  // namespace state
}  // namespace muse

namespace std {

template <>
struct hash<muse::state::QueryKey> {
  std::size_t operator()(const muse::state::QueryKey& k) const noexcept {
    return std::hash<std::string>{}(k.str());
  }
};

template <>
struct hash<muse::state::Tag> {
  std::size_t operator()(const muse::state::Tag& t) const noexcept {
    return std::hash<std::string>{}(t.str());
  }
};

}  // namespace std

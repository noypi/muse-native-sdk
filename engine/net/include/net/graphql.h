#pragma once

// GraphQL client over the ITransport seam: query/mutate over POST,
// subscriptions over SSE, with automatic persisted queries (APQ),
// request batching, and a normalized-cache key shape
// (FR-NET-009 / AC-NET-017 / RULE-NET-003).
//
// Every failure surfaces as a non-empty Result::error — empty query,
// oversized input, transport failure/throw, GraphQL errors, malformed
// bodies — and nothing here throws. Resume replays only missed events:
// setLastEventId(id) stores the id, subscribe() sends it as the
// `Last-Event-ID` request header, and the server replays what follows.
// Cache integration is the key shape only (GraphQLClient::cacheKey);
// the NET-001 normalized cache itself is not rebuilt here.

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "net/transport.h"
#include "net/types.h"

namespace muse {
namespace net {

class GraphQLClient {
 public:
  struct Result {
    std::string data;
    std::string error;
  };

  class Subscription {
   public:
    // Returns true per update (out filled, error empty), false at end or
    // after cancel. Never throws; a null out is a structured miss.
    bool next(Result* out);
    void cancel();

    // Adopted additive extension: re-seeds the last-seen event id on an
    // existing subscription (affects LastEventId, not the wire — resume
    // across connections goes through GraphQLClient::setLastEventId
    // before re-subscribing).
    void setLastEventId(std::string id);

   private:
    friend class GraphQLClient;
    friend std::string LastEventId(const Subscription&);
    Subscription(std::vector<std::string> payloads, std::vector<std::string> ids,
                 std::string terminal_error);
    std::vector<std::string> payloads_;
    std::vector<std::string> ids_;
    std::size_t pos_ = 0;
    std::string last_event_id_;
    std::string terminal_error_;
    bool cancelled_ = false;
  };

  explicit GraphQLClient(std::shared_ptr<ITransport> transport);

  Result query(std::string q, std::string vars);
  Result mutate(std::string m, std::string vars);
  std::unique_ptr<Subscription> subscribe(std::string q, std::string vars);

  void enableApq(bool enabled);
  void enableBatching(std::size_t max);

  // Resume seed: sent as the `Last-Event-ID` header by subscribe().
  void setLastEventId(std::string id);

  // Normalized-cache key shape: stable per (query, vars), varies with
  // either. Key shape only; no cache I/O happens here.
  static std::string cacheKey(const std::string& query, const std::string& vars);

 private:
  struct Pending {
    std::string query;
    std::string vars;
  };

  Result executeSingle(const std::string& query, const std::string& vars);
  Result flushBatch();
  static std::string queryError(const std::string& query);

  std::shared_ptr<ITransport> transport_;
  bool apq_ = false;
  bool batching_ = false;
  std::size_t batch_max_ = 0;
  std::vector<Pending> pending_;
  std::string last_event_id_;
};

// Latest event id observed by a subscription (empty before any update).
// Never throws.
std::string LastEventId(const GraphQLClient::Subscription& sub);

}  // namespace net
}  // namespace muse

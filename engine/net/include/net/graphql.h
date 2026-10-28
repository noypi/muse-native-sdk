#pragma once

// GraphQL client over the ITransport seam: query/mutate over POST,
// subscriptions over SSE, with automatic persisted queries (APQ),
// request batching, and a normalized-cache key shape
// (FR-NET-009 / AC-NET-017 / RULE-NET-003).
//
// Resume replays only missed events: setLastEventId(id) stores the id,
// subscribe() sends it as the `Last-Event-ID` request header, and events
// up to and including the stored id (matched by string equality) are
// dropped so delivery has zero duplicates. Absent-id policy: when the
// stored id is not present in the replayed stream, nothing is dropped —
// the full replay delivers (fail-open, never a silent loss).
// Cache integration is the key shape only (GraphQLClient::cacheKey);
// the NET-001 normalized cache itself is not rebuilt here.
//
// Batching is explicit-async: query()/mutate() always execute a single
// operation immediately (batching never changes their behavior); batched
// work goes through queue() + flush(), where flush() performs one
// transport call and returns per-caller Results in request order. There
// is no provisional empty success.

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
  // Retained for API compatibility; intentionally a no-op. Batching is
  // explicit-async (queue() + flush()) and needs no arming: query()/
  // mutate() always execute immediately and flush() serves whatever is
  // queued, so there is no flag for them to consult.
  void enableBatching(std::size_t max);

  // Batching split: buffer one operation for the next flush(). Never
  // executes I/O; never returns a provisional success.
  void queue(std::string q, std::string vars);
  // Execute all queued operations in one transport call, returning one
  // Result per queued operation in request order. Empty queue → single
  // Result with a non-empty error. Never throws.
  std::vector<Result> flush();

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
  std::vector<Result> flushBatch();
  static std::string queryError(const std::string& query);
  static std::string varsError(const std::string& vars);

  std::shared_ptr<ITransport> transport_;
  bool apq_ = false;
  std::vector<Pending> pending_;
  std::string last_event_id_;
};

// Latest event id observed by a subscription (empty before any update).
// Never throws.
std::string LastEventId(const GraphQLClient::Subscription& sub);

}  // namespace net
}  // namespace muse

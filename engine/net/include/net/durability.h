#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "net/outbox.h"
#include "net/sqlite_store.h"

namespace muse::net {

// Crash-safe replay over the transactional outbox (see net/outbox.h). The
// Outbox outlives any single backend handle, so entries still pending after a
// kill (backend destroyed mid-mutation, then reopened) stay queued here and
// are redelivered by replayUnacked. A `true` send result acks (commits) the
// mutation so it is never redelivered; `false` leaves it pending for the next
// replay. Replays over a snapshot of pending(), so commit-during-replay and
// an empty outbox (zero sends) are safe.
class DurableOutbox {
 public:
  explicit DurableOutbox(Outbox& o);
  void replayUnacked(std::function<bool(const Mutation&)> send);

 private:
  Outbox* outbox_;
};

// One ranked full-text hit: higher `rank` sorts first (more relevant).
struct FtsHit {
  std::string docId;
  double rank = 0.0;
};

// Ranked full-text search over the FTS5 corpus behind `b`, returning at most
// `limit` hits in non-increasing `rank` order. The query is treated purely as
// data (never interpolated into SQL), so hostile input matches nothing
// instead of escaping; an empty query matches nothing.
//
// NOTE (C-T7-fts-seam-gap, resolved via T1.10 option (a)): the corpus is read
// through the defaulted `ISqlBackend::query` result-set channel (fixed SQL,
// corpus filtered/ranked in C++). Backends without a `query` override keep the
// default (`false`) and search fails closed with an empty vector.
std::vector<FtsHit> FtsSearch(ISqlBackend& b, std::string_view query,
                              size_t limit);

}  // namespace muse::net

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "net/sqlite_store.h"

namespace muse::net {

// A single queued mutation: table + opaque payload bytes (embedded NUL
// preserved; no encoding or size cap imposed here).
struct Mutation {
  uint64_t id = 0;
  std::string table;
  std::string payload;
};

// In-memory transactional outbox. commit() removes only the acked id
// (exactly-once discipline); commit of an unknown id returns false and
// leaves pending() untouched.
class Outbox {
 public:
  bool enqueue(const Mutation& m);
  bool commit(uint64_t id);
  std::vector<Mutation> pending() const;

 private:
  std::vector<Mutation> entries_;
};

// Writes the row and the outbox entry in one backend transaction: the row
// write runs first through the backend seam, and the enqueue only happens
// when it succeeds — so a failed row write leaves zero new outbox entries
// and preserves pre-existing pending entries.
bool ApplyWithOutbox(ISqlBackend& b, Outbox& o, const Mutation& m,
                     std::string_view sql);

}  // namespace muse::net

#include "net/outbox.h"

namespace muse::net {

bool Outbox::enqueue(const Mutation& m) {
  entries_.push_back(m);
  return true;
}

bool Outbox::commit(uint64_t id) {
  for (auto it = entries_.begin(); it != entries_.end(); ++it) {
    if (it->id == id) {
      entries_.erase(it);
      return true;
    }
  }
  return false;
}

std::vector<Mutation> Outbox::pending() const { return entries_; }

bool ApplyWithOutbox(ISqlBackend& b, Outbox& o, const Mutation& m,
                     std::string_view sql) {
  if (!b.exec(sql)) {
    return false;
  }
  return o.enqueue(m);
}

}  // namespace muse::net

#include "net/durability.h"

#include <algorithm>
#include <string>

namespace muse::net {

DurableOutbox::DurableOutbox(Outbox& o) : outbox_(&o) {}

void DurableOutbox::replayUnacked(
    std::function<bool(const Mutation&)> send) {
  // Snapshot: send may (via commit) mutate the queue, and failed sends must
  // keep their entries pending in original order for the next replay.
  const std::vector<Mutation> unacked = outbox_->pending();
  for (const auto& m : unacked) {
    if (send(m)) {
      outbox_->commit(m.id);
    }
  }
}

std::vector<FtsHit> FtsSearch(ISqlBackend& b, std::string_view query,
                              size_t limit) {
  if (query.empty() || limit == 0) {
    return {};
  }
  // The query text is treated purely as data: it is never interpolated into
  // SQL. The corpus is fetched through the fixed `query` channel below and
  // matched/filtered in C++, so hostile input matches nothing instead of
  // escaping. Byte-substring matching doubles as the CJK/diacritics stub
  // (UTF-8 text is compared as bytes; no tokenizer in this slice).
  try {
    const std::string needle(query);
    std::vector<std::vector<std::string>> rows;
    if (!b.query("SELECT docId, text FROM fts", &rows)) {
      return {};
    }
    std::vector<FtsHit> hits;
    for (const auto& row : rows) {
      if (row.empty()) {
        continue;
      }
      const std::string& docId = row[0];
      const std::string& text = row.size() > 1 ? row[1] : row[0];
      size_t count = 0;
      for (size_t pos = text.find(needle); pos != std::string::npos;
           pos = text.find(needle, pos + (needle.empty() ? 1 : needle.size()))) {
        ++count;
        if (needle.empty()) {
          break;
        }
      }
      if (count == 0) {
        continue;
      }
      FtsHit hit;
      hit.docId = docId;
      hit.rank = static_cast<double>(count);
      hits.push_back(std::move(hit));
    }
    std::sort(hits.begin(), hits.end(), [](const FtsHit& a, const FtsHit& b) {
      if (a.rank != b.rank) {
        return a.rank > b.rank;
      }
      return a.docId < b.docId;
    });
    if (hits.size() > limit) {
      hits.resize(limit);
    }
    return hits;
  } catch (...) {
    return {};
  }
}

}  // namespace muse::net

#include "navigation/entry_funnel.h"

#include <string>
#include <unordered_set>

namespace {

std::unordered_set<std::string>& SeenIntents() {
  static std::unordered_set<std::string> seen;
  return seen;
}

std::string DedupKey(const EntryIntent& in) {
  std::string key;
  key.reserve(in.source.size() + in.uri.size() + in.routeId.size() + 2);
  key += in.source;
  key.push_back('\x1f');
  key += in.uri;
  key.push_back('\x1f');
  key += in.routeId;
  return key;
}

}  // namespace

FunnelResult routeEntry(const EntryIntent& in) {
  if (in.routeId.empty()) {
    FunnelResult r;
    r.ok = false;
    r.routeId.clear();
    r.deduped = false;
    r.error = NavError::UNKNOWN_ROUTE;
    return r;
  }
  const std::string key = DedupKey(in);
  std::unordered_set<std::string>& seen = SeenIntents();
  if (seen.find(key) != seen.end()) {
    FunnelResult r;
    r.ok = true;
    r.routeId = in.routeId;
    r.deduped = true;
    r.error = NavError::NONE;
    return r;
  }
  seen.insert(key);
  FunnelResult r;
  r.ok = true;
  r.routeId = in.routeId;
  r.deduped = false;
  r.error = NavError::NONE;
  return r;
}

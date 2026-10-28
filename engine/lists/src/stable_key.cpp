#include "lists/stable_key.h"

#include <cstdio>
#include <cstdlib>
#include <unordered_set>

namespace lists {

KeyCheck checkKeys(const std::vector<ItemKey>& keys, bool isDev) {
  std::unordered_set<std::string> seen;
  std::unordered_set<std::string> reported;
  KeyCheck out;
  out.ok = true;
  bool unstable = false;

  for (const ItemKey& item : keys) {
    if (item.key.empty()) {
      unstable = true;
      continue;
    }
    if (!seen.insert(item.key).second && reported.insert(item.key).second) {
      ItemKey dup;
      dup.key = item.key;
      out.duplicates.push_back(dup);
    }
  }
  if (unstable) {
    // Report the unstable marker once, like a duplicate offender.
    ItemKey dup;
    dup.key = "";
    out.duplicates.push_back(dup);
  }
  out.ok = out.duplicates.empty();
  if (!out.ok && isDev) {
    std::fprintf(stderr, "lists::checkKeys: %zu duplicate/unstable key(s):",
                 out.duplicates.size());
    for (const ItemKey& dup : out.duplicates) {
      std::fprintf(stderr, " \"%s\"", dup.key.c_str());
    }
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
    std::abort();
  }
  return out;
}

}  // namespace lists

#include "lists/collection_order.h"

#include <unordered_map>

namespace lists {

bool VerifyLogicalOrder(const CollectionNode& n, const BindLog& binds) {
  if (binds.bindSequence.size() != n.items.size()) return false;
  std::unordered_map<std::string, int> counts;
  counts.reserve(n.items.size() * 2 + 1);
  for (const auto& item : n.items) ++counts[item.key];
  for (const auto& bound : binds.bindSequence) {
    auto it = counts.find(bound.key);
    if (it == counts.end() || it->second == 0) return false;
    --it->second;
  }
  return true;
}

}  // namespace lists

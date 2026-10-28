#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "lists/stable_key.h"

namespace lists {

// Ordered key list backing a recycler window. Each entry is the stable
// identity string from `ItemKey::key` (FR-TSX-005 key-identity contract,
// see `lists/stable_key.h`): diffing, lookup, and reorder resolve by this
// key, never by index. No new key type is introduced here.
struct KeyedList {
  std::vector<std::string> keys;
};

enum class EditType { Insert, Remove, Move };

// One delta operation. `from` is the old-list index (Remove/Move),
// `to` is the new-list index (Insert/Move).
struct EditOp {
  EditType type = EditType::Insert;
  std::string key;
  size_t from = 0;
  size_t to = 0;
};

// Minimal edit script from `myersDiff`, plus the committed order that
// `applyBatch` installs atomically. `result` always equals the new list's
// key order; `ops` describes the same transition minimally (one op per
// inserted/removed key, one Move per relocated key, unchanged keys absent).
struct EditScript {
  std::vector<EditOp> ops;
  std::vector<std::string> result;
};

// Record of one committed batch: the installed order and how many items
// were (re)bound by that batch (inserts + moves).
struct Batch {
  std::vector<std::string> order;
  size_t rebinds = 0;
};

// Minimal bound-item window over a KeyedList. Constructed from the old
// list, then advanced only through `applyBatch`, which commits a whole
// script at once — the recycler is never observed partially applied.
class Recycler {
 public:
  explicit Recycler(const KeyedList& initial);

  const std::vector<std::string>& order() const;
  // Number of items (re)bound so far: one per inserted or moved key.
  // Pre-existing items that neither moved nor were inserted are never
  // rebound by a batch.
  size_t rebindCount() const;

  friend Batch applyBatch(Recycler& r, EditScript e);

 private:
  std::vector<std::string> order_;
  size_t rebinds_ = 0;
};

// Myers O(ND) diff over stable keys, lifted so a same-key remove+insert
// pair becomes a single Move. Pure function of its inputs: no shared
// mutable state, no UI-thread affinity — safe to call from a core worker
// pool thread (NFR-LISTS-002 context).
EditScript myersDiff(const KeyedList& oldL, const KeyedList& newL);

// Commits `e` to `r` atomically: installs `e.result` as the new order in
// one step and accounts one (re)bind per Insert/Move op.
Batch applyBatch(Recycler& r, EditScript e);

}  // namespace lists

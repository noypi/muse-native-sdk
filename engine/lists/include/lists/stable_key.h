#pragma once

#include <string>
#include <vector>

namespace lists {

// Stable identity of a list item across frames. Consumes the FR-TSX-005
// key-identity contract without re-implementing compiler key plumbing:
// lookup, diff, and reorder resolve by `key`, never by index. An empty
// string carries no stable identity and is treated as unstable.
struct ItemKey {
  std::string key;
};

inline bool operator==(const ItemKey& a, const ItemKey& b) {
  return a.key == b.key;
}

inline bool operator!=(const ItemKey& a, const ItemKey& b) {
  return !(a == b);
}

// Result of validating a frame's key list. When `ok` is false,
// `duplicates` holds each offending key exactly once, in order of first
// repeat: every key seen more than once, plus one empty-key entry when any
// unstable (empty) key is present.
struct KeyCheck {
  bool ok = true;
  std::vector<ItemKey> duplicates;
};

// Validates `keys` for duplicate or unstable entries. Unique keys pass in
// both modes. In dev (`isDev=true`) any offender aborts loudly via
// stderr + abort (a contract violation the caller cannot swallow); in
// release it returns `{false, offenders}` with no abort.
KeyCheck checkKeys(const std::vector<ItemKey>& keys, bool isDev);

}  // namespace lists

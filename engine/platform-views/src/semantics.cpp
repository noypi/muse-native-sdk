#include "pviews/semantics.h"

namespace {

bool containsId(const SemNode* data, std::size_t n, std::uint64_t id) noexcept {
  for (std::size_t i = 0; i < n; ++i) {
    if (data[i].id == id) return true;
  }
  return false;
}

}  // namespace

MergeResult mergeSemantics(const NodeList& engineTree,
                           const NodeList& platformTree,
                           MergedTree& out) noexcept {
  // Duplicate scan first so no partial merge is published on error.
  const SemNode* engine = engineTree.data;
  const SemNode* platform = platformTree.data;
  const std::size_t eSize = (engine == nullptr) ? 0 : engineTree.size;
  const std::size_t pSize = (platform == nullptr) ? 0 : platformTree.size;

  for (std::size_t i = 0; i < eSize; ++i) {
    for (std::size_t j = 0; j < i; ++j) {
      if (engine[j].id == engine[i].id) {
        MergeResult r;
        r.ok = false;
        r.count = 0;
        r.error = SemErrorCode::SEM_DUPLICATE_NODE;
        r.duplicateId = engine[i].id;
        out.size = 0;
        return r;
      }
    }
  }
  for (std::size_t i = 0; i < pSize; ++i) {
    if (containsId(engine, eSize, platform[i].id)) {
      MergeResult r;
      r.ok = false;
      r.count = 0;
      r.error = SemErrorCode::SEM_DUPLICATE_NODE;
      r.duplicateId = platform[i].id;
      out.size = 0;
      return r;
    }
    for (std::size_t j = 0; j < i; ++j) {
      if (platform[j].id == platform[i].id) {
        MergeResult r;
        r.ok = false;
        r.count = 0;
        r.error = SemErrorCode::SEM_DUPLICATE_NODE;
        r.duplicateId = platform[i].id;
        out.size = 0;
        return r;
      }
    }
  }

  // Engine-then-platform order into the caller-provided fixed buffer.
  std::size_t n = 0;
  if (out.data != nullptr) {
    for (std::size_t i = 0; i < eSize && n < out.capacity; ++i) {
      out.data[n++] = engine[i];
    }
    for (std::size_t i = 0; i < pSize && n < out.capacity; ++i) {
      out.data[n++] = platform[i];
    }
  }
  out.size = n;

  MergeResult r;
  r.ok = true;
  r.count = n;
  r.error = SemErrorCode::NONE;
  r.duplicateId = 0;
  return r;
}

std::uint64_t semanticOwner(const MergedTree& merged,
                            std::uint64_t id) noexcept {
  if (merged.data == nullptr) return UINT64_MAX;
  for (std::size_t i = 0; i < merged.size; ++i) {
    if (merged.data[i].id == id) return merged.data[i].owner;
  }
  return UINT64_MAX;
}

bool mergedContainsOnce(const MergedTree& merged, std::uint64_t id) noexcept {
  if (merged.data == nullptr) return false;
  std::size_t hits = 0;
  for (std::size_t i = 0; i < merged.size; ++i) {
    if (merged.data[i].id == id && ++hits > 1) return false;
  }
  return hits == 1;
}

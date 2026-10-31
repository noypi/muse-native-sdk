#pragma once

#include <cstddef>
#include <cstdint>

struct SemNode {
  std::uint64_t id = 0;
  std::uint64_t owner = 0;
};

struct NodeList {
  SemNode* data = nullptr;
  std::size_t size = 0;
};

struct MergedTree {
  SemNode* data = nullptr;
  std::size_t size = 0;
  std::size_t capacity = 0;
};

enum class SemErrorCode {
  NONE = 0,
  SEM_DUPLICATE_NODE = 1,
  SEM_BUFFER_TOO_SMALL = 2,
};

struct MergeResult {
  bool ok = false;
  std::size_t count = 0;
  SemErrorCode error = SemErrorCode::NONE;
  std::uint64_t duplicateId = 0;
};

MergeResult mergeSemantics(const NodeList& engineTree,
                           const NodeList& platformTree,
                           MergedTree& out) noexcept;
std::uint64_t semanticOwner(const MergedTree& merged,
                            std::uint64_t id) noexcept;
bool mergedContainsOnce(const MergedTree& merged, std::uint64_t id) noexcept;

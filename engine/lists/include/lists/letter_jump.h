#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace lists {

// One indexed section: `letter` is the section key (case-insensitive,
// normalized to uppercase at map-build time); `firstItemIndex` and
// `pixelOffset` are the landing position for a jump to this section.
struct Section {
  std::string id;
  char letter = 'A';
  size_t firstItemIndex = 0;
  float pixelOffset = 0.0f;
};

// Ordered section list in logical (letter) order.
struct SectionList {
  std::vector<Section> sections;
};

// Resolved jump destination: the section to land on plus its offset.
struct SectionHit {
  std::string sectionId;
  size_t firstItemIndex = 0;
  float pixelOffset = 0.0f;
};

// Prebuilt section->offset map. `BuildIndexMap` resolves every letter A-Z
// once (nearest surviving section, ties to the earlier section); `JumpToLetter`
// is then a single array probe with no scan of the section list.
struct IndexMap {
  std::array<SectionHit, 26> hits{};
  bool empty = true;
};

// Builds the map once from `s`. Empty input yields an empty map whose jumps
// resolve to `{"", 0, 0.0f}` and never crash.
IndexMap BuildIndexMap(const SectionList& s);

// Resolves `letter` (case-insensitive) in O(1) map probes: one uppercase
// normalization plus one array probe. A letter with no section falls back to
// the nearest surviving section precomputed at build time (ties to the
// earlier section; single-section maps land every letter there).
SectionHit JumpToLetter(const IndexMap& m, char letter);

}  // namespace lists

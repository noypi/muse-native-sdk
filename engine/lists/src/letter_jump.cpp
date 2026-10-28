#include "lists/letter_jump.h"

#include <cstdlib>

namespace lists {
namespace {

char Upper(char c) {
  if (c >= 'a' && c <= 'z') return static_cast<char>(c - 'a' + 'A');
  return c;
}

bool IsUpperLetter(char c) { return c >= 'A' && c <= 'Z'; }

}  // namespace

IndexMap BuildIndexMap(const SectionList& s) {
  IndexMap m;
  if (s.sections.empty()) {
    m.empty = true;
    for (auto& h : m.hits) h = SectionHit{};
    return m;
  }
  m.empty = false;
  // Normalize section letters once; keep original order for tie-breaking
  // (earlier section wins on equal distance).
  std::vector<char> keys;
  keys.reserve(s.sections.size());
  for (const auto& sec : s.sections) keys.push_back(Upper(sec.letter));
  // Precompute the nearest surviving section for every letter A-Z so jump
  // time is a single probe regardless of list size.
  for (char L = 'A'; L <= 'Z'; ++L) {
    size_t best = 0;
    int bestDist = std::abs(static_cast<int>(L) - static_cast<int>(keys[0]));
    for (size_t i = 1; i < keys.size(); ++i) {
      int d = std::abs(static_cast<int>(L) - static_cast<int>(keys[i]));
      if (d < bestDist) {
        bestDist = d;
        best = i;
      }
    }
    const Section& sec = s.sections[best];
    m.hits[static_cast<size_t>(L - 'A')] = SectionHit{
        sec.id, sec.firstItemIndex, sec.pixelOffset};
  }
  return m;
}

SectionHit JumpToLetter(const IndexMap& m, char letter) {
  if (m.empty) return SectionHit{};
  char u = Upper(letter);
  // Non-alpha input clamps to the nearest edge slot; still one probe.
  if (!IsUpperLetter(u)) {
    u = (u < 'A') ? 'A' : 'Z';
  }
  return m.hits[static_cast<size_t>(u - 'A')];
}

}  // namespace lists

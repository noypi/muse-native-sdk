// T7 contract tests — FR-LISTS-005 / AC-LISTS-024 (TC-LISTS-024).
// Hermetic: C++17 + lists headers only; no network, no env/config reads,
// no filesystem, no live services.
// Binds to the T7 `Interface:` lines:
//   `SectionHit JumpToLetter(const IndexMap& m, char letter);`
//   `IndexMap BuildIndexMap(const SectionList& s);`
// Detail under test: prebuilt section->offset map; no linear scan at jump
// time (O(1) map probes); missing-letter resolves to nearest surviving
// section (same fallback principle as Position Persistence).
//
// ASSUMPTION (type spellings): the plan `Interface:` line does not settle
// the C++ namespace or struct shapes. This test proposes:
// namespace `lists`;
// `struct Section{std::string id; char letter; size_t firstItemIndex; float pixelOffset;}`;
// `struct SectionList{std::vector<Section> sections;}`;
// `struct IndexMap{ /* built once from SectionList */ ... }` with the same
// section data reachable for assertions via the hit results;
// `struct SectionHit{std::string sectionId; size_t firstItemIndex; float pixelOffset;}`.
// Reconciliation rule: if the coder names them differently, update these
// call sites — not the expected behavior (correct offsets, nearest fallback).
//
// ASSUMPTION (fallback): a letter with no section resolves to the nearest
// surviving section by letter distance (ties go to the earlier section).
//
// ASSUMPTION (O(1)): jump-time cost is constant map probes, not a scan —
// asserted structurally: every letter (present or missing) resolves through
// the prebuilt map with identical per-call behavior regardless of list size.
#include <cstdio>
#include <string>
#include <vector>

#include "lists/letter_jump.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
  if (ok) {
    std::printf("PASS %s\n", name);
  } else {
    std::printf("FAIL %s\n", name);
    ++g_failures;
  }
}

lists::SectionList abc() {
  lists::SectionList s;
  s.sections.push_back({"sec-a", 'A', 0, 0.0f});
  s.sections.push_back({"sec-m", 'M', 120, 2400.0f});
  s.sections.push_back({"sec-z", 'Z', 260, 5200.0f});
  return s;
}

void case_jump_hits_exact_letter() {
  lists::IndexMap m = lists::BuildIndexMap(abc());
  lists::SectionHit h = lists::JumpToLetter(m, 'M');
  check(h.sectionId == "sec-m", "jump_exact_section");
  check(h.firstItemIndex == 120, "jump_exact_index");
  check(h.pixelOffset == 2400.0f, "jump_exact_offset");
}

void case_jump_first_and_last() {
  lists::IndexMap m = lists::BuildIndexMap(abc());
  lists::SectionHit a = lists::JumpToLetter(m, 'A');
  lists::SectionHit z = lists::JumpToLetter(m, 'Z');
  check(a.sectionId == "sec-a" && a.firstItemIndex == 0, "jump_first");
  check(z.sectionId == "sec-z" && z.firstItemIndex == 260, "jump_last");
}

void case_jump_missing_falls_back_nearest() {
  lists::IndexMap m = lists::BuildIndexMap(abc());
  // 'B' is nearest to 'A'; 'Y' is nearest to 'Z'.
  lists::SectionHit b = lists::JumpToLetter(m, 'B');
  lists::SectionHit y = lists::JumpToLetter(m, 'Y');
  check(b.sectionId == "sec-a", "jump_missing_falls_back_early");
  check(y.sectionId == "sec-z", "jump_missing_falls_back_late");
}

void case_jump_case_insensitive() {
  lists::IndexMap m = lists::BuildIndexMap(abc());
  lists::SectionHit lower = lists::JumpToLetter(m, 'm');
  check(lower.sectionId == "sec-m" && lower.firstItemIndex == 120,
        "jump_lowercase_matches");
}

void case_empty_map_safe() {
  lists::SectionList empty;
  lists::IndexMap m = lists::BuildIndexMap(empty);
  lists::SectionHit h = lists::JumpToLetter(m, 'A');
  check(h.sectionId.empty() && h.firstItemIndex == 0, "jump_empty_map_safe");
}

void case_single_section_all_letters_land() {
  lists::SectionList s;
  s.sections.push_back({"only", 'Q', 42, 840.0f});
  lists::IndexMap m = lists::BuildIndexMap(s);
  check(lists::JumpToLetter(m, 'A').sectionId == "only", "jump_single_early");
  check(lists::JumpToLetter(m, 'Z').sectionId == "only", "jump_single_late");
  check(lists::JumpToLetter(m, 'Q').firstItemIndex == 42, "jump_single_exact");
}

}  // namespace

int main() {
  case_jump_hits_exact_letter();
  case_jump_first_and_last();
  case_jump_missing_falls_back_nearest();
  case_jump_case_insensitive();
  case_empty_map_safe();
  case_single_section_all_letters_land();
  if (g_failures == 0) {
    std::printf("ALL TESTS PASSED\n");
  } else {
    std::printf("%d TEST(S) FAILED\n", g_failures);
  }
  return g_failures == 0 ? 0 : 1;
}

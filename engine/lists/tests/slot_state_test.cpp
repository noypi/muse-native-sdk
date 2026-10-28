// T9 contract tests — FR-LISTS-005 / AC-LISTS-026 (TC-LISTS-026).
// Hermetic: C++17 + lists headers only; no network, no env/config reads,
// no filesystem, no live services.
// Binds to the T9 `Interface:` lines:
//   `SlotView ResolveSlot(CollectionState st);`
//   maps `{Empty, Loading, Error, Content+Footer}` to the slot node;
//   `ScrollPos PreserveAcrossSwitch(ScrollPos before, CollectionState from,`
//   ` CollectionState to);` returns the restored offset.
// Detail under test: slots are states of the same collection node (same
// scrollable, same semantics node); state switches preserve position where
// applicable.
//
// ASSUMPTION (type spellings): the plan `Interface:` line does not settle
// the C++ namespace or struct shapes. This test proposes:
// namespace `lists`;
// `enum class CollectionState{Empty, Loading, Error, Content, ContentWithFooter};`
// (`ContentWithFooter` encodes the plan's `Content+Footer`);
// `struct SlotView{std::string slot; bool scrollable;}`;
// `struct ScrollPos{float offset; int leadingIndex;}`.
// Reconciliation rule: if the coder names them differently, update these
// call sites — not the expected behavior (slot mapping + position preserve).
//
// ASSUMPTION (scrollable): every slot reports `scrollable == true` — slots
// share the same scrollable/semantics node per the plan detail.
//
// ASSUMPTION (preserve): switches between non-content states and into
// content preserve the incoming offset/leading index; switching into Empty
// resets to the top (offset 0, index 0) since there is no content position.
#include <cstdio>
#include <string>

#include "lists/slot_state.h"

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

void case_slots_resolve() {
  check(lists::ResolveSlot(lists::CollectionState::Empty).slot == "empty",
        "slot_empty");
  check(lists::ResolveSlot(lists::CollectionState::Loading).slot == "loading",
        "slot_loading");
  check(lists::ResolveSlot(lists::CollectionState::Error).slot == "error",
        "slot_error");
  check(lists::ResolveSlot(lists::CollectionState::Content).slot == "content",
        "slot_content");
  check(lists::ResolveSlot(lists::CollectionState::ContentWithFooter).slot ==
            "content+footer",
        "slot_content_footer");
}

void case_slots_share_scrollable() {
  bool all = lists::ResolveSlot(lists::CollectionState::Empty).scrollable &&
             lists::ResolveSlot(lists::CollectionState::Loading).scrollable &&
             lists::ResolveSlot(lists::CollectionState::Error).scrollable &&
             lists::ResolveSlot(lists::CollectionState::Content).scrollable &&
             lists::ResolveSlot(lists::CollectionState::ContentWithFooter)
                 .scrollable;
  check(all, "slots_all_scrollable");
}

void case_switch_preserves_position() {
  lists::ScrollPos before{420.0f, 17};
  lists::ScrollPos after = lists::PreserveAcrossSwitch(
      before, lists::CollectionState::Loading, lists::CollectionState::Content);
  check(after.offset == 420.0f && after.leadingIndex == 17,
        "switch_loading_content_preserves");
}

void case_content_to_error_preserves() {
  lists::ScrollPos before{900.0f, 33};
  lists::ScrollPos after = lists::PreserveAcrossSwitch(
      before, lists::CollectionState::Content, lists::CollectionState::Error);
  check(after.offset == 900.0f && after.leadingIndex == 33,
        "switch_content_error_preserves");
}

void case_switch_into_empty_resets() {
  lists::ScrollPos before{900.0f, 33};
  lists::ScrollPos after = lists::PreserveAcrossSwitch(
      before, lists::CollectionState::Content, lists::CollectionState::Empty);
  check(after.offset == 0.0f && after.leadingIndex == 0,
        "switch_into_empty_resets");
}

void case_footer_switch_preserves() {
  lists::ScrollPos before{150.0f, 5};
  lists::ScrollPos after = lists::PreserveAcrossSwitch(
      before, lists::CollectionState::Content,
      lists::CollectionState::ContentWithFooter);
  check(after.offset == 150.0f && after.leadingIndex == 5,
        "switch_content_footer_preserves");
}

}  // namespace

int main() {
  case_slots_resolve();
  case_slots_share_scrollable();
  case_switch_preserves_position();
  case_content_to_error_preserves();
  case_switch_into_empty_resets();
  case_footer_switch_preserves();
  if (g_failures == 0) {
    std::printf("ALL TESTS PASSED\n");
  } else {
    std::printf("%d TEST(S) FAILED\n", g_failures);
  }
  return g_failures == 0 ? 0 : 1;
}

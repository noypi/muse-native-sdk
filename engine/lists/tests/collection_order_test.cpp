// T14 contract tests — FR-LISTS-006 / AC-LISTS-031 (TC-LISTS-031).
// Hermetic: C++17 + lists headers only; no network, no env/config reads,
// no filesystem, no live services.
// Binds to the T14 `Interface:` line:
//   `bool VerifyLogicalOrder(const CollectionNode& n, const BindLog& binds);`
//   returns true iff exposed semantics order equals logical index order
//   irrespective of bind/prefetch sequence.
// Detail under test: semantics snapshot sorted by logical index at publish
// time; recycling/prefetch interleavings must not perturb it.
//
// ASSUMPTION (type spellings): the plan `Interface:` line does not settle
// the C++ namespace or struct shapes. This test proposes:
// namespace `lists`; `CollectionNode` from `lists/collection_semantics.h`
// with an ordered `items` member (`std::vector<ItemKey>`); `struct BindLog{
// std::vector<ItemKey> bindSequence;}` in `lists/collection_order.h`.
// Reconciliation rule: if the coder names them differently, update these
// call sites — not the expected behavior (order equality vs set equality).
//
// ASSUMPTION (semantics): the check compares the exposed semantics against
// the node's logical `items` as the same multiset, each exactly once;
// arrival order ignored. A missing key, an extra key, a duplicate key,
// or an empty log against a non-empty node all return false. An empty log
// against an empty node returns true.
#include <cstdio>
#include <string>
#include <vector>

#include "lists/collection_order.h"
#include "lists/collection_semantics.h"
#include "lists/stable_key.h"

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

lists::CollectionNode node5() {
  lists::CollectionNode n;
  n.totalCount = 5;
  n.realizedCount = 5;
  n.leadingIndex = 0;
  for (int i = 0; i < 5; ++i) n.items.push_back(lists::ItemKey{"k" + std::to_string(i)});
  return n;
}

lists::BindLog log_of(std::vector<std::string> keys) {
  lists::BindLog b;
  for (const auto& k : keys) b.bindSequence.push_back(lists::ItemKey{k});
  return b;
}

void case_in_order_true() {
  auto n = node5();
  check(lists::VerifyLogicalOrder(n, log_of({"k0", "k1", "k2", "k3", "k4"})),
        "in_order_true");
}

void case_reverse_bind_true() {
  // Adversarial: binds arrive reversed; exposed order must still be logical.
  auto n = node5();
  check(lists::VerifyLogicalOrder(n, log_of({"k4", "k3", "k2", "k1", "k0"})),
        "reverse_bind_true");
}

void case_strided_bind_true() {
  auto n = node5();
  check(lists::VerifyLogicalOrder(n, log_of({"k0", "k2", "k4", "k1", "k3"})),
        "strided_bind_true");
}

void case_prefetch_interleave_true() {
  // Nested-prefetch interleave: far-ahead keys bind first.
  auto n = node5();
  check(lists::VerifyLogicalOrder(n, log_of({"k4", "k0", "k3", "k1", "k2"})),
        "prefetch_interleave_true");
}

void case_wrong_set_false() {
  auto n = node5();
  check(!lists::VerifyLogicalOrder(n, log_of({"k0", "k1", "k2", "k3", "kX"})),
        "wrong_key_false");
}

void case_missing_key_false() {
  auto n = node5();
  check(!lists::VerifyLogicalOrder(n, log_of({"k0", "k1", "k2", "k3"})),
        "missing_key_false");
}

void case_extra_key_false() {
  auto n = node5();
  check(!lists::VerifyLogicalOrder(n, log_of({"k0", "k1", "k2", "k3", "k4", "k5"})),
        "extra_key_false");
}

void case_duplicate_key_false() {
  auto n = node5();
  check(!lists::VerifyLogicalOrder(n, log_of({"k0", "k1", "k2", "k3", "k3"})),
        "duplicate_key_false");
}

void case_empty_both_true() {
  lists::CollectionNode n;
  n.totalCount = 0;
  n.realizedCount = 0;
  n.leadingIndex = 0;
  lists::BindLog b;
  check(lists::VerifyLogicalOrder(n, b), "empty_true");
}

void case_empty_log_nonempty_false() {
  auto n = node5();
  lists::BindLog b;
  check(!lists::VerifyLogicalOrder(n, b), "empty_log_false");
}

}  // namespace

int main() {
  case_in_order_true();
  case_reverse_bind_true();
  case_strided_bind_true();
  case_prefetch_interleave_true();
  case_wrong_set_false();
  case_missing_key_false();
  case_extra_key_false();
  case_duplicate_key_false();
  case_empty_both_true();
  case_empty_log_nonempty_false();
  if (g_failures == 0) {
    std::printf("ALL TESTS PASSED\n");
  } else {
    std::printf("%d TEST(S) FAILED\n", g_failures);
  }
  return g_failures == 0 ? 0 : 1;
}

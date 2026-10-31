// T5 acceptance check (tester-owned, NOT a unit test).
// Asserts mergeSemantics never silently drops nodes when the caller buffer
// is undersized: AC-PVIEWS-011 requires each node exactly once, so a
// capacity shortfall must be a typed denial (ok == false, out.size == 0),
// never ok == true with nodes missing.
// Usage (from the _run checkout root):
//   g++ -std=c++17 t5-acceptance/check_truncation.cpp \
//       engine/platform-views/src/semantics.cpp \
//       -I engine/platform-views/include -o /tmp/check_truncation && /tmp/check_truncation
// Exit 0 only if every assertion below holds; prints PASS/FAIL per check.
#include "pviews/semantics.h"

#include <cstdio>

static int failures = 0;

static void check(const char* name, bool cond) {
  if (cond) {
    std::printf("PASS %s\n", name);
  } else {
    std::printf("FAIL %s\n", name);
    ++failures;
  }
}

int main() {
  // 4 distinct nodes, capacity for only 2.
  SemNode engine[2] = {{1, 0}, {2, 0}};
  SemNode platform[2] = {{3, 1}, {4, 1}};
  SemNode buf[2] = {};
  NodeList engineTree{engine, 2};
  NodeList platformTree{platform, 2};
  MergedTree out{buf, 0, 2};

  MergeResult r = mergeSemantics(engineTree, platformTree, out);

  // Required behavior: deny, publish nothing partial.
  check("undersized buffer denied (ok == false)", !r.ok);
  check("no partial merge published (out.size == 0)", out.size == 0);
  check("denial carries a non-NONE error code",
        r.error != SemErrorCode::NONE);

  if (failures == 0) {
    std::printf("TRUNCATION-CHECK-OK\n");
  } else {
    std::printf("TRUNCATION-CHECK-FAIL (%d checks failed)\n", failures);
  }
  return failures == 0 ? 0 : 1;
}

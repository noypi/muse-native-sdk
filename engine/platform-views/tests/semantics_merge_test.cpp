// T5 contract tests — FR-PVIEWS-009 / AC-PVIEWS-011 / TC-PVIEWS-011.
// Bind ONLY to the plan T5 `Interface:` line; never to implementation.
// Hermetic: C++17 + pviews/semantics.h (to-be-created) only; integer-fake
// node ids/owners, no TalkBack, no Compose/View classes, no device,
// no network, no env/config reads, no filesystem, no live services.
// Errors are return values, never thrown: every error-path case also
// proves no exception escapes (try/catch around the call).
//
// RED state (Batch 3, pre-coder): pviews/semantics.h and src/semantics.cpp
// do not exist yet, so this file does NOT compile. That compile failure
// is the expected red for every case below.
//
// ASSUMED header shape (see unit-tests/T5.md; coder must satisfy it;
// helpers beyond the `Interface:` line are marked ASSUMPTION there):
//   struct SemNode { uint64_t id; uint64_t owner; };  // owner: 0=engine, 1=platform; id 0 == invalid
//   struct NodeList { SemNode* data; size_t size; };  // non-owning view over caller arrays
//   struct MergedTree { SemNode* data; size_t size; size_t capacity; };
//       // ASSUMPTION: caller-provided fixed buffer merge target; merge fills
//       // data[0..size) and never allocates (hermetic, no OOM path).
//   enum class SemErrorCode { NONE = 0, SEM_DUPLICATE_NODE = 1, SEM_BUFFER_TOO_SMALL = 2 };
//   struct MergeResult { bool ok; size_t count; SemErrorCode error; uint64_t duplicateId; };
//   MergeResult mergeSemantics(const NodeList& engineTree, const NodeList& platformTree,
//                              MergedTree& out) noexcept;
//   uint64_t semanticOwner(const MergedTree& merged, uint64_t id) noexcept;
//       // ASSUMPTION: test-only routing probe; returns owner of id, or
//       // UINT64_MAX when absent. Proves "actions dispatch to correct owner".
//   bool mergedContainsOnce(const MergedTree& merged, uint64_t id) noexcept;
//       // ASSUMPTION: observable dedupe probe; true iff id appears exactly once.
#include <cstdint>
#include <cstdio>

#include "pviews/semantics.h"

static const uint64_t kEngine = 0;
static const uint64_t kPlatform = 1;
static const uint64_t kAbsent = UINT64_MAX;

static NodeList makeList(SemNode* data, size_t n) {
  NodeList l;
  l.data = data;
  l.size = n;
  return l;
}

static MergedTree makeOut(SemNode* data, size_t capacity) {
  MergedTree m;
  m.data = data;
  m.size = 0;
  m.capacity = capacity;
  return m;
}

// ---- Happy path: disjoint trees merge, each node once ----

// Case: disjoint engine + platform nodes -> ok, count == sum, each id
// present exactly once.
static bool check_disjoint_merge_ok() {
  try {
    SemNode engine[] = {{{1}, {kEngine}}, {{2}, {kEngine}}};
    SemNode platform[] = {{{3}, {kPlatform}}, {{4}, {kPlatform}}};
    SemNode buf[8] = {};
    NodeList e = makeList(engine, 2);
    NodeList p = makeList(platform, 2);
    MergedTree out = makeOut(buf, 8);
    const MergeResult r = mergeSemantics(e, p, out);
    return r.ok && r.error == SemErrorCode::NONE && r.count == 4 &&
           out.size == 4 && mergedContainsOnce(out, 1) &&
           mergedContainsOnce(out, 2) && mergedContainsOnce(out, 3) &&
           mergedContainsOnce(out, 4);
  } catch (...) {
    return false;
  }
}

// Case: both trees empty -> ok, count 0.
static bool check_both_empty_ok() {
  try {
    SemNode buf[4] = {};
    NodeList e = makeList(nullptr, 0);
    NodeList p = makeList(nullptr, 0);
    MergedTree out = makeOut(buf, 4);
    const MergeResult r = mergeSemantics(e, p, out);
    return r.ok && r.error == SemErrorCode::NONE && r.count == 0 &&
           out.size == 0;
  } catch (...) {
    return false;
  }
}

// Case: empty engine + platform nodes -> ok, platform nodes preserved.
static bool check_platform_only_ok() {
  try {
    SemNode platform[] = {{{7}, {kPlatform}}};
    SemNode buf[4] = {};
    NodeList e = makeList(nullptr, 0);
    NodeList p = makeList(platform, 1);
    MergedTree out = makeOut(buf, 4);
    const MergeResult r = mergeSemantics(e, p, out);
    return r.ok && r.count == 1 && mergedContainsOnce(out, 7) &&
           semanticOwner(out, 7) == kPlatform;
  } catch (...) {
    return false;
  }
}

// Case: engine nodes only -> ok, engine ownership preserved.
static bool check_engine_only_ok() {
  try {
    SemNode engine[] = {{{8}, {kEngine}}};
    SemNode buf[4] = {};
    NodeList e = makeList(engine, 1);
    NodeList p = makeList(nullptr, 0);
    MergedTree out = makeOut(buf, 4);
    const MergeResult r = mergeSemantics(e, p, out);
    return r.ok && r.count == 1 && mergedContainsOnce(out, 8) &&
           semanticOwner(out, 8) == kEngine;
  } catch (...) {
    return false;
  }
}

// ---- Action routing: owning side ----

// Case: engine-owned node routes to engine.
static bool check_action_routes_to_engine() {
  try {
    SemNode engine[] = {{{10}, {kEngine}}};
    SemNode platform[] = {{{11}, {kPlatform}}};
    SemNode buf[4] = {};
    MergedTree out = makeOut(buf, 4);
    const MergeResult r =
        mergeSemantics(makeList(engine, 1), makeList(platform, 1), out);
    if (!r.ok) return false;
    return semanticOwner(out, 10) == kEngine;
  } catch (...) {
    return false;
  }
}

// Case: platform-owned node routes to platform.
static bool check_action_routes_to_platform() {
  try {
    SemNode engine[] = {{{10}, {kEngine}}};
    SemNode platform[] = {{{11}, {kPlatform}}};
    SemNode buf[4] = {};
    MergedTree out = makeOut(buf, 4);
    const MergeResult r =
        mergeSemantics(makeList(engine, 1), makeList(platform, 1), out);
    if (!r.ok) return false;
    return semanticOwner(out, 11) == kPlatform;
  } catch (...) {
    return false;
  }
}

// Case: unknown id -> absent sentinel (no phantom routing).
static bool check_unknown_id_absent() {
  try {
    SemNode engine[] = {{{10}, {kEngine}}};
    SemNode buf[4] = {};
    MergedTree out = makeOut(buf, 4);
    const MergeResult r =
        mergeSemantics(makeList(engine, 1), makeList(nullptr, 0), out);
    if (!r.ok) return false;
    return semanticOwner(out, 999) == kAbsent &&
           !mergedContainsOnce(out, 999);
  } catch (...) {
    return false;
  }
}

// ---- Error path: SEM_DUPLICATE_NODE { id } ----

// Case: duplicate id inside engineTree -> SEM_DUPLICATE_NODE with id,
// never throws.
static bool check_duplicate_within_engine_denied() {
  try {
    SemNode engine[] = {{{20}, {kEngine}}, {{20}, {kEngine}}};
    SemNode buf[4] = {};
    MergedTree out = makeOut(buf, 4);
    const MergeResult r =
        mergeSemantics(makeList(engine, 2), makeList(nullptr, 0), out);
    return !r.ok && r.error == SemErrorCode::SEM_DUPLICATE_NODE &&
           r.duplicateId == 20;
  } catch (...) {
    return false;  // errors are values, never exceptions
  }
}

// Case: duplicate id inside platformTree -> SEM_DUPLICATE_NODE with id,
// never throws.
static bool check_duplicate_within_platform_denied() {
  try {
    SemNode platform[] = {{{21}, {kPlatform}}, {{21}, {kPlatform}}};
    SemNode buf[4] = {};
    MergedTree out = makeOut(buf, 4);
    const MergeResult r =
        mergeSemantics(makeList(nullptr, 0), makeList(platform, 2), out);
    return !r.ok && r.error == SemErrorCode::SEM_DUPLICATE_NODE &&
           r.duplicateId == 21;
  } catch (...) {
    return false;
  }
}

// Case: same id registered on both sides (cross-boundary double
// registration) -> SEM_DUPLICATE_NODE with id, never throws.
static bool check_duplicate_across_boundary_denied() {
  try {
    SemNode engine[] = {{{30}, {kEngine}}};
    SemNode platform[] = {{{30}, {kPlatform}}};
    SemNode buf[4] = {};
    MergedTree out = makeOut(buf, 4);
    const MergeResult r =
        mergeSemantics(makeList(engine, 1), makeList(platform, 1), out);
    return !r.ok && r.error == SemErrorCode::SEM_DUPLICATE_NODE &&
           r.duplicateId == 30;
  } catch (...) {
    return false;
  }
}

// ---- Error path: SEM_BUFFER_TOO_SMALL (F1 fix: never truncate) ----

// Case: caller buffer smaller than total nodes -> typed denial, nothing
// published (ok==false, count==0, out.size==0), never throws.
static bool check_undersized_buffer_denied() {
  try {
    SemNode engine[] = {{{1}, {kEngine}}, {{2}, {kEngine}}};
    SemNode platform[] = {{{3}, {kPlatform}}};
    SemNode buf[2] = {};
    MergedTree out = makeOut(buf, 2);
    const MergeResult r =
        mergeSemantics(makeList(engine, 2), makeList(platform, 1), out);
    return !r.ok && r.count == 0 &&
           r.error == SemErrorCode::SEM_BUFFER_TOO_SMALL &&
           out.size == 0;
  } catch (...) {
    return false;  // errors are values, never exceptions
  }
}

// Case: null caller buffer with non-empty input -> typed denial, nothing
// published (ok==false, count==0, out.size==0), never throws.
static bool check_null_buffer_denied() {
  try {
    SemNode engine[] = {{{1}, {kEngine}}};
    MergedTree out;
    out.data = nullptr;
    out.size = 0;
    out.capacity = 0;
    const MergeResult r =
        mergeSemantics(makeList(engine, 1), makeList(nullptr, 0), out);
    return !r.ok && r.count == 0 &&
           r.error == SemErrorCode::SEM_BUFFER_TOO_SMALL &&
           out.size == 0;
  } catch (...) {
    return false;  // errors are values, never exceptions
  }
}

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_disjoint_merge_ok);
  RUN_CASE(check_both_empty_ok);
  RUN_CASE(check_platform_only_ok);
  RUN_CASE(check_engine_only_ok);
  RUN_CASE(check_action_routes_to_engine);
  RUN_CASE(check_action_routes_to_platform);
  RUN_CASE(check_unknown_id_absent);
  RUN_CASE(check_duplicate_within_engine_denied);
  RUN_CASE(check_duplicate_within_platform_denied);
  RUN_CASE(check_duplicate_across_boundary_denied);
  RUN_CASE(check_undersized_buffer_denied);
  RUN_CASE(check_null_buffer_denied);
  if (failures > 0) {
    std::printf("semantics_merge_test: %d failing\n", failures);
    return 1;
  }
  std::printf("semantics_merge_test: all passing\n");
  return 0;
}

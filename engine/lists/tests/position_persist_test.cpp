// T5 contract tests — FR-LISTS-003 / AC-LISTS-011 (TC-LISTS-011;
// consumes FR-STATE-001 STATE-PERSIST + NAV-RESTORE contract).
// Hermetic: C++17 + lists headers only; synthetic inline key lists;
// no network, no env/config reads, no filesystem, no live services,
// no wall-clock sleeps. Injected offsets/indices only.
// Binds to the T5 `Interface:` line:
//   `Snapshot savePos(DestId d, float offsetPx, int index, ItemKey anchor,
//    float anchorFrac); Restored restorePos(DestId d, const Snapshot& s,
//    const KeyedList& data);`
//   — restorePos falls back to nearest surviving index when anchor gone,
//   never out-of-range; in-flight velocity never restored (rest-at-rest).
// Include path follows the engine convention:
//   #include "lists/position_persist.h" with engine/lists/include on path,
//   plus consumed T1/T4 headers ("lists/stable_key.h", "lists/myers_diff.h")
//   for ItemKey/KeyedList — reused, never redefined.
//
// ASSUMPTION (type spellings): the plan/Interface line settles the two
// function signatures but not the C++ spelling of DestId, Snapshot, or
// Restored. This test proposes: namespace `lists`;
// `DestId{std::string id}`; `Snapshot{DestId dest; float offsetPx;
// int index; ItemKey anchor; float anchorFrac}`;
// `Restored{float offsetPx; int index; float velocityPxS}` where
// `velocityPxS` is always 0.0f (rest-at-rest: velocity never restored).
// Reconciliation rule: if the coder names them differently, update these
// call sites — not the expected behavior (round-trip offset+anchor,
// nearest-surviving-index fallback, in-range always, velocity dropped).
//
// ASSUMPTION (nearest surviving index): "nearest" means the surviving key
// whose new-list position is closest to the saved index, ties broken toward
// the lower index, clamped to [0, size-1]; empty data restores index 0 and
// offset 0. A negative saved offset restores clamped to >= 0.
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "lists/myers_diff.h"
#include "lists/position_persist.h"
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

lists::DestId dest(const char* s) {
  lists::DestId d{};
  d.id = s;
  return d;
}

lists::KeyedList kl(std::initializer_list<const char*> ids) {
  lists::KeyedList l{};
  for (const char* id : ids) l.keys.emplace_back(id);
  return l;
}

lists::ItemKey key(const char* s) {
  lists::ItemKey k{};
  k.key = s;
  return k;
}

bool in_range(int index, const lists::KeyedList& data) {
  if (data.keys.empty()) return index == 0;
  return index >= 0 && static_cast<size_t>(index) < data.keys.size();
}

void case_roundtrip_offset_and_anchor() {
  lists::KeyedList data = kl({"a", "b", "c", "d", "e"});
  lists::Snapshot s =
      lists::savePos(dest("home"), 250.0f, 2, key("c"), 0.5f);
  lists::Restored r = lists::restorePos(dest("home"), s, data);
  check(r.offsetPx == 250.0f, "roundtrip_offset_preserved");
  check(r.index == 2, "roundtrip_index_preserved");
  check(in_range(r.index, data), "roundtrip_index_in_range");
}

void case_save_passthrough() {
  lists::Snapshot s = lists::savePos(dest("home"), 120.0f, 1, key("b"), 0.25f);
  check(s.offsetPx == 120.0f, "save_offset_recorded");
  check(s.index == 1, "save_index_recorded");
  check(s.anchor == key("b"), "save_anchor_recorded");
  check(s.anchorFrac == 0.25f, "save_anchor_frac_recorded");
}

void case_deleted_anchor_nearest_surviving() {
  // Anchor "c" at index 2 is removed; "d" slides to index 2 — nearest.
  lists::KeyedList data = kl({"a", "b", "d", "e"});
  lists::Snapshot s = lists::savePos(dest("home"), 250.0f, 2, key("c"), 0.5f);
  lists::Restored r = lists::restorePos(dest("home"), s, data);
  check(r.index == 2, "deleted_anchor_nearest_index");
  check(in_range(r.index, data), "deleted_anchor_in_range");
}

void case_deleted_anchor_near_start() {
  // Anchor "a" at index 0 is gone; nearest survivor is index 0.
  lists::KeyedList data = kl({"b", "c", "d"});
  lists::Snapshot s = lists::savePos(dest("home"), 10.0f, 0, key("a"), 0.0f);
  lists::Restored r = lists::restorePos(dest("home"), s, data);
  check(r.index == 0, "deleted_first_anchor_clamps_to_zero");
  check(in_range(r.index, data), "deleted_first_anchor_in_range");
}

void case_deleted_anchor_at_end() {
  // Anchor "e" at index 4 is gone with only two survivors; clamp to last.
  lists::KeyedList data = kl({"a", "b"});
  lists::Snapshot s = lists::savePos(dest("home"), 900.0f, 4, key("e"), 0.9f);
  lists::Restored r = lists::restorePos(dest("home"), s, data);
  check(r.index == 1, "deleted_last_anchor_clamps_to_last");
  check(in_range(r.index, data), "deleted_last_anchor_in_range");
}

void case_empty_data() {
  lists::KeyedList data = kl({});
  lists::Snapshot s = lists::savePos(dest("home"), 250.0f, 2, key("c"), 0.5f);
  lists::Restored r = lists::restorePos(dest("home"), s, data);
  check(r.index == 0, "empty_data_index_zero");
  check(r.offsetPx == 0.0f, "empty_data_offset_zero");
}

void case_velocity_dropped() {
  lists::KeyedList data = kl({"a", "b", "c", "d"});
  lists::Snapshot s =
      lists::savePos(dest("home"), 300.0f, 3, key("d"), 0.75f);
  lists::Restored r = lists::restorePos(dest("home"), s, data);
  check(r.velocityPxS == 0.0f, "velocity_dropped_rest_at_rest");
}

void case_negative_offset_clamped() {
  lists::KeyedList data = kl({"a", "b", "c"});
  lists::Snapshot s =
      lists::savePos(dest("home"), -50.0f, 0, key("a"), 0.0f);
  lists::Restored r = lists::restorePos(dest("home"), s, data);
  check(r.offsetPx >= 0.0f, "negative_offset_clamped");
  check(in_range(r.index, data), "negative_offset_index_in_range");
}

void case_per_destination_isolation() {
  lists::KeyedList data = kl({"a", "b", "c"});
  lists::Snapshot home = lists::savePos(dest("home"), 100.0f, 1, key("b"), 0.1f);
  lists::Snapshot search =
      lists::savePos(dest("search"), 400.0f, 2, key("c"), 0.8f);
  lists::Restored rh = lists::restorePos(dest("home"), home, data);
  lists::Restored rs = lists::restorePos(dest("search"), search, data);
  check(rh.offsetPx == 100.0f, "per_dest_home_offset");
  check(rs.offsetPx == 400.0f, "per_dest_search_offset");
  check(rs.index == 2, "per_dest_search_index");
}

void case_t1_keys_consumed() {
  // T1 is consumed, not redefined: the anchor keys used above are valid.
  lists::KeyCheck c =
      lists::checkKeys({key("a"), key("b"), key("c")}, false);
  check(c.ok, "t1_keys_still_pass");
}

void case_dest_mismatch_restores_rest() {
  // Review follow-up (coder choice): destination mismatch restores to rest
  // (offset 0 / index 0 / no velocity) — a snapshot from destination A
  // never leaks its position into destination B.
  lists::KeyedList data = kl({"a", "b", "c"});
  lists::Snapshot sA = lists::savePos(dest("a"), 250.0f, 2, key("c"), 0.5f);
  lists::Restored r = lists::restorePos(dest("b"), sA, data);
  check(r.offsetPx == 0.0f && r.index == 0 && r.velocityPxS == 0.0f,
        "dest_mismatch_restores_rest");
  check(r.offsetPx != sA.offsetPx, "dest_mismatch_no_offset_leak");
}

void case_nan_offset_restores_rest() {
  // Security hardening: a corrupt snapshot with a NaN offset (e.g.
  // carry-over from the pre-fix NaN era) must not leak NaN downstream —
  // `NaN < 0` is false, so without the `!isfinite` clamp it took the
  // pass-through branch. Coder choice pinned: clamp to rest offset 0,
  // index resolved normally (anchor hit here) and in range.
  lists::KeyedList data = kl({"a", "b", "c"});
  lists::Snapshot s =
      lists::savePos(dest("home"), std::numeric_limits<float>::quiet_NaN(),
                     1, key("b"), 0.5f);
  lists::Restored r = lists::restorePos(dest("home"), s, data);
  check(r.offsetPx == 0.0f, "nan_offset_restores_zero");
  check(in_range(r.index, data), "nan_offset_index_in_range");
}

}  // namespace

int main() {
  case_roundtrip_offset_and_anchor();
  case_save_passthrough();
  case_deleted_anchor_nearest_surviving();
  case_deleted_anchor_near_start();
  case_deleted_anchor_at_end();
  case_empty_data();
  case_velocity_dropped();
  case_negative_offset_clamped();
  case_per_destination_isolation();
  case_t1_keys_consumed();
  case_dest_mismatch_restores_rest();
  case_nan_offset_restores_rest();
  if (g_failures == 0) {
    std::printf("ALL TESTS PASSED\n");
  } else {
    std::printf("%d TEST(S) FAILED\n", g_failures);
  }
  return g_failures == 0 ? 0 : 1;
}

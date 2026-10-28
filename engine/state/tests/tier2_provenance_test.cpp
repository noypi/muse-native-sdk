// Batch 2 (Tier-2 native path, FR-STATE-003) contract tests — T3.
// Spec refs: FR-STATE-003, AC-STATE-014 (TC-STATE-014).
// Hermetic: C++17 + committed engine/state headers only; no network, no env/
// config reads, no filesystem, no live services. In-process state only.
// Binds ONLY to the T3 `Interface:` lines; never to implementation.
//
// ASSUMPTIONS (see unit-tests/T3.md):
//   A-T3-1: the coder adds `Provenance::kTier2` to `muse::state::Provenance`
//     (staged atom.hpp currently defines only kJs). This file assumes the
//     enumerator exists after the coder's change.
//   A-T3-2: `provenance.hpp` includes (transitively) `atom.hpp` and does NOT
//     redefine `Provenance`; the enum lives in exactly one header.
//   A-T3-3: `tier2Write` on an unknown AtomId returns `Status::kNotFound`
//     (the `Status` return exists to surface it; spec is silent on the exact
//     code, so non-ok is the binding assertion and kNotFound is the assumed
//     code).
#include <cstdio>

#include "muse/state/atom.hpp"
#include "muse/state/provenance.hpp"

namespace {

using muse::state::AtomId;
using muse::state::AtomStore;
using muse::state::ObserverId;
using muse::state::Provenance;
using muse::state::Status;
using muse::state::TypedValue;

// Tier-2 write lands and provenanceOf reports kTier2 (TC-STATE-014 core).
static bool check_tier2_write_lands_and_reports_tier2() {
  AtomStore s;
  AtomId id = s.create(TypedValue(1));
  if (tier2Write(s, id, TypedValue(2)) != Status::kOk) return false;
  auto got = s.read(id, static_cast<ObserverId>(1));
  if (!got.ok() || !(got.value() == TypedValue(2))) return false;
  return provenanceOf(s, id) == Provenance::kTier2;
}

// Plain JS write still reports kJs (no regression on the JS path).
static bool check_js_write_reports_js_provenance() {
  AtomStore s;
  AtomId id = s.create(TypedValue(1));
  s.write(id, TypedValue(5), Provenance::kJs);
  auto got = s.read(id, static_cast<ObserverId>(1));
  if (!got.ok() || !(got.value() == TypedValue(5))) return false;
  return provenanceOf(s, id) == Provenance::kJs;
}

// Interleaved JS then Tier-2: both land, provenance tracks the last writer,
// final value equals last-writer-wins order.
static bool check_interleaved_js_then_tier2_last_writer_wins() {
  AtomStore s;
  AtomId id = s.create(TypedValue(0));
  s.write(id, TypedValue(10), Provenance::kJs);
  if (provenanceOf(s, id) != Provenance::kJs) return false;
  if (tier2Write(s, id, TypedValue(20)) != Status::kOk) return false;
  if (provenanceOf(s, id) != Provenance::kTier2) return false;
  auto got = s.read(id, static_cast<ObserverId>(1));
  return got.ok() && got.value() == TypedValue(20);
}

// Interleaved Tier-2 then JS: provenance flips back, value is the JS write.
static bool check_interleaved_tier2_then_js_last_writer_wins() {
  AtomStore s;
  AtomId id = s.create(TypedValue(0));
  if (tier2Write(s, id, TypedValue(20)) != Status::kOk) return false;
  if (provenanceOf(s, id) != Provenance::kTier2) return false;
  s.write(id, TypedValue(30), Provenance::kJs);
  if (provenanceOf(s, id) != Provenance::kJs) return false;
  auto got = s.read(id, static_cast<ObserverId>(1));
  return got.ok() && got.value() == TypedValue(30);
}

// tier2Write funnels through the identical AtomStore::write path: one
// tier2Write coalesces into exactly one batch, same as a JS write.
static bool check_tier2_write_coalesces_into_single_batch() {
  AtomStore s;
  AtomId id = s.create(TypedValue(1));
  s.read(id, static_cast<ObserverId>(1));
  if (tier2Write(s, id, TypedValue(2)) != Status::kOk) return false;
  if (s.flush().size() != 1) return false;
  return s.flush().empty();
}

// Provenance is per-atom: two atoms track independent writers, both within
// one store and across stores (same AtomId value in distinct stores must not
// alias — store-scoped query per the T1.10 verdict).
static bool check_provenance_is_per_atom_independent() {
  AtomStore s;
  AtomId a = s.create(TypedValue(1));
  AtomId b = s.create(TypedValue(1));
  s.write(a, TypedValue(2), Provenance::kJs);
  if (tier2Write(s, b, TypedValue(3)) != Status::kOk) return false;
  if (!(provenanceOf(s, a) == Provenance::kJs &&
        provenanceOf(s, b) == Provenance::kTier2))
    return false;
  // Across stores: id values restart per store, so id 0 in s2 aliases id 0
  // in s by value only; provenance must be scoped to its own store.
  AtomStore s2;
  AtomId c = s2.create(TypedValue(1));
  if (tier2Write(s2, c, TypedValue(9)) != Status::kOk) return false;
  return provenanceOf(s2, c) == Provenance::kTier2 &&
         provenanceOf(s, a) == Provenance::kJs;
}

// Boundary/error: tier2Write to an unknown AtomId must not crash and must
// report a non-ok Status (assumed Status::kNotFound per A-T3-3).
static bool check_tier2_write_unknown_id_reports_not_found() {
  AtomStore s;
  Status st = tier2Write(s, static_cast<AtomId>(999999), TypedValue(1));
  if (st == Status::kOk) return false;
  return st == Status::kNotFound;
}

}  // namespace

#define RUN_CASE(name)                      \
  do {                                      \
    if (!(name)()) {                        \
      std::printf("FAIL %s\n", #name);      \
      ++failures;                           \
    } else {                                \
      std::printf("PASS %s\n", #name);       \
    }                                       \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_tier2_write_lands_and_reports_tier2);
  RUN_CASE(check_js_write_reports_js_provenance);
  RUN_CASE(check_interleaved_js_then_tier2_last_writer_wins);
  RUN_CASE(check_interleaved_tier2_then_js_last_writer_wins);
  RUN_CASE(check_tier2_write_coalesces_into_single_batch);
  RUN_CASE(check_provenance_is_per_atom_independent);
  RUN_CASE(check_tier2_write_unknown_id_reports_not_found);
  if (failures > 0) {
    std::printf("tier2_provenance_test: %d failing\n", failures);
    return 1;
  }
  std::printf("tier2_provenance_test: all passing\n");
  return 0;
}

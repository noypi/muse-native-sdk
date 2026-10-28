#pragma once

#include "muse/state/atom.hpp"

namespace muse {
namespace state {

// Tier-2 native write: funnels through the identical AtomStore::write path
// used by JS, passing Provenance::kTier2. No parallel native-only setter
// exists. Returns Status::kNotFound for an unknown AtomId, never UB.
Status tier2Write(AtomStore& s, AtomId id, TypedValue v);

// Reports the origin of the last write to the atom in this store.
// Store-scoped: reads the per-atom last-writer recording from `s`, so the
// same AtomId value in distinct stores never aliases. Defaults to
// Provenance::kJs for unknown ids or atoms with no recorded Tier-2 write.
Provenance provenanceOf(const AtomStore& s, AtomId id);

}  // namespace state
}  // namespace muse

// The contract tests call tier2Write/provenanceOf unqualified. Both take an
// AtomStore argument, so they resolve via ADL; they are also surfaced at
// global scope to match the test-file spelling. Both names denote the
// muse::state entities above.
using muse::state::provenanceOf;
using muse::state::tier2Write;

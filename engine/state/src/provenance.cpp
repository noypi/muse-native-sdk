#include "muse/state/provenance.hpp"

namespace muse {
namespace state {

// Reserved observer id for the existence probe in tier2Write. AtomStore has
// no contains() query; StatusOr<TypedValue> AtomStore::read is the fallible
// read shape, so a read probes existence before writing.
constexpr ObserverId kExistenceProbeObserver = 0;

Status tier2Write(AtomStore& s, AtomId id, TypedValue v) {
  if (!s.read(id, kExistenceProbeObserver).ok()) {
    return Status::kNotFound;
  }
  s.write(id, v, Provenance::kTier2);
  return Status::kOk;
}

Provenance provenanceOf(const AtomStore& s, AtomId id) {
  return s.provenance(id);
}

}  // namespace state
}  // namespace muse

#include "muse/state/restore.hpp"

namespace muse {
namespace state {
namespace persist {

StatusOr<AppState> restoreAfterDeath(Snapshot s, const MigrationRegistry& r,
                                     TelemetrySink* sink, FieldDecryptor* d) {
  // Bit-flip rejection first: checksum over the stored payload. Emits exactly
  // one kSnapshotCorrupt event on mismatch (null sink safe); the corrupt path
  // carries no value so callers observe a clean default via the non-ok status.
  RestoreOutcome checked = loadChecked(s, sink);
  if (!checked.clean) return Status::kCorrupt;

  // Previous-schema snapshots migrate forward during restore.
  StatusOr<Snapshot> migrated = migrate(s, r);
  if (!migrated.ok()) return migrated.status();
  Bytes payload = migrated.value().payload;

  // Optional decrypt hook (nullable/incidental identity in tests).
  if (d != nullptr) payload = d->decrypt(payload);

  // Opaque slots: every slot is present after a clean restore.
  AppState out;
  out.stores = payload;
  out.nav = payload;
  out.scroll = payload;
  out.focus.valid = true;
  out.focusValid = true;
  return out;
}

}  // namespace persist
}  // namespace state
}  // namespace muse

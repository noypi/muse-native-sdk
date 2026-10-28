#ifndef GRAPHICS_CAPS_WORKAROUND_DB_H_
#define GRAPHICS_CAPS_WORKAROUND_DB_H_

#include <exception>
#include <string>
#include <vector>

#include "caps/caps_tier.h"

namespace graphics {

// Opaque version token for the active workaround DB. `version` is the
// stringified form of the DB file's numeric `version` field.
struct DbVersion {
  std::string version;
};

// Schema error for every DB load failure (bad version, malformed JSON,
// missing/unreadable file, oversized payload). `what()` is exactly
// "DB_SCHEMA_MISMATCH" so callers can match on it without parsing.
class DbSchemaMismatchError : public std::exception {
 public:
  const char* what() const noexcept override { return "DB_SCHEMA_MISMATCH"; }
};

// Reloads the workaround DB from `path`, swapping the active DB without a
// rebuild. Returns the new version. Throws DbSchemaMismatchError on any
// schema/IO failure; on failure the previously active DB is left untouched.
DbVersion reload_db(const std::string& path);

// Returns the workaround flags whose vendor/driver/GPU constraints all match
// `fingerprint`. Entries with an empty constraint match any value for that
// constraint; entries with an empty workaround_flag never match. `feature`
// and `engine_version` are stored on each entry but intentionally not
// matched: DeviceFingerprint carries no engine-version/feature field, so a
// non-empty value in those fields is recorded but always matches.
std::vector<std::string> match_workarounds(
    const DeviceFingerprint& fingerprint);

}  // namespace graphics

#endif  // GRAPHICS_CAPS_WORKAROUND_DB_H_

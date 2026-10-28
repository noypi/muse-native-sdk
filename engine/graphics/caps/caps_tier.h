#ifndef GRAPHICS_CAPS_CAPS_TIER_H_
#define GRAPHICS_CAPS_CAPS_TIER_H_

#include <string>
#include <vector>

namespace graphics {

// Device fingerprint: identity + probed capability flags + RAM class.
// Field set per A-T4-FIELDS (FR-GRAPHICS-005 DB schema + NFR-GRAPHICS-003).
struct DeviceFingerprint {
  std::string vendor;
  std::string gpu_id;
  std::string driver_version;
  int ram_gb = 0;
  bool vk13_dynamic_rendering = false;
  bool vk110_available = false;
  bool gles32_available = false;
};

enum class CapsTier {
  LOW,
  MID,
  HIGH,
};

// Telemetry record for a fingerprint: derived tier, workaround set, and a
// stable opaque hash identifying the fingerprint.
struct CapsReport {
  CapsTier tier = CapsTier::MID;
  std::vector<std::string> workarounds;
  std::string fingerprint_hash;
};

// Deterministic hash of a fingerprint: fixed-length lowercase hex embedding
// no raw fingerprint material. Defined in telemetry.cc.
std::string fingerprint_hash_for(const DeviceFingerprint& fingerprint);

// Derives the capability tier from probed features/properties + RAM class.
// Pure and deterministic: the same fingerprint always yields an identical
// report. Workarounds default to empty at T4 scope; T5 wires matching.
CapsReport resolve_tier(const DeviceFingerprint& fingerprint);

}  // namespace graphics

#endif  // GRAPHICS_CAPS_CAPS_TIER_H_

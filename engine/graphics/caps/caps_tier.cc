#include "caps/caps_tier.h"

namespace graphics {

namespace {

// Tier derivation per A-T4-TIER: HIGH requires advertised Vulkan 1.3 dynamic
// rendering with RAM >= 6GB; LOW when RAM < 4GB or when only GLES 3.2 is
// available; MID otherwise. RAM cutoffs from NFR-GRAPHICS-003.
CapsTier derive_tier(const DeviceFingerprint& fp) {
  if (fp.vk13_dynamic_rendering && fp.ram_gb >= 6) {
    return CapsTier::HIGH;
  }
  if (fp.ram_gb < 4) {
    return CapsTier::LOW;
  }
  const bool gles_only =
      !fp.vk13_dynamic_rendering && !fp.vk110_available && fp.gles32_available;
  if (gles_only) {
    return CapsTier::LOW;
  }
  return CapsTier::MID;
}

}  // namespace

CapsReport resolve_tier(const DeviceFingerprint& fingerprint) {
  CapsReport report;
  report.tier = derive_tier(fingerprint);
  // A-T4-WA: no workaround DB is wired at T4 scope, so the default set is
  // empty; T5 wires match_workarounds matching. Deterministic either way.
  report.workarounds = {};
  report.fingerprint_hash = fingerprint_hash_for(fingerprint);
  return report;
}

}  // namespace graphics

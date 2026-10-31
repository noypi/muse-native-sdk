#ifndef ENGINE_GRAPHICS_QUALITY_QUALITY_TELEMETRY_H_
#define ENGINE_GRAPHICS_QUALITY_QUALITY_TELEMETRY_H_

#include <cstdint>
#include <string>

namespace quality_telemetry {

// Mirrors quality_tier::QualityTier enumerators (same four tiers).
enum class QualityTier { kLow, kMedium, kHigh, kUltra };

enum class Status { kOk, kInvalidArg, kInvalidTier };

struct TelemetryRecord {
  QualityTier tier = QualityTier::kLow;
  const char* tierName = "LOW";
  std::string reason;
  std::string fingerprintHash;
  std::uint64_t sequence = 0;
  std::uint64_t timestampMs = 0;
  Status status = Status::kOk;
};

TelemetryRecord reportTier(QualityTier current, const char* reason);

Status overrideTier(QualityTier app, QualityTier* active);

void clearOverride();

TelemetryRecord lastRecord();

}  // namespace quality_telemetry

#endif  // ENGINE_GRAPHICS_QUALITY_QUALITY_TELEMETRY_H_

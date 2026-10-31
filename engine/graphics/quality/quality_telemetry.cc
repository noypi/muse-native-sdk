#include "engine/graphics/quality/quality_telemetry.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace quality_telemetry {
namespace {

constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr std::uint64_t kOffsetA = 14695981039346656037ULL;
constexpr std::uint64_t kOffsetB = 1099511628211ULL ^ 0x9e3779b97f4a7c15ULL;

void HashMix(std::uint64_t& h, const char* data, std::uint64_t len) {
  for (std::uint64_t i = 0; i < len; ++i) {
    h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(data[i]));
    h *= kFnvPrime;
  }
}

// Canonical length-prefixed string mix, mirroring the fingerprint_hash_for
// convention in caps/caps_tier.h: "<len>:" + bytes + '\0' separator.
void HashString(std::uint64_t& h, const char* data, std::uint64_t len) {
  char len_buf[32];
  const int n = std::snprintf(
      len_buf, sizeof(len_buf), "%llu:", static_cast<unsigned long long>(len));
  HashMix(h, len_buf, static_cast<std::uint64_t>(n));
  if (len > 0) {
    HashMix(h, data, len);
  }
  const char sep = '\0';
  HashMix(h, &sep, 1);
}

std::uint64_t FingerprintPass(const char* tier_token,
                              std::uint64_t tier_len, const char* reason,
                              std::uint64_t reason_len, std::uint64_t seed) {
  std::uint64_t h = seed;
  HashString(h, tier_token, tier_len);
  HashString(h, reason, reason_len);
  return h;
}

// Deterministic 128-bit digest over canonical length-prefixed (tier,reason)
// bytes, rendered as 32 lowercase hex chars. Reuses the fingerprint_hash_for
// convention (two FNV-1a/64 passes, distinct offsets); caps headers are NOT
// included. Carries no raw tier/reason material beyond the digest.
std::string FingerprintFor(const char* tier_token, const char* reason) {
  std::uint64_t tier_len = 0;
  while (tier_token[tier_len] != '\0') ++tier_len;
  std::uint64_t reason_len = 0;
  while (reason[reason_len] != '\0') ++reason_len;
  const std::uint64_t hi =
      FingerprintPass(tier_token, tier_len, reason, reason_len, kOffsetA);
  const std::uint64_t lo =
      FingerprintPass(tier_token, tier_len, reason, reason_len, kOffsetB);
  char out[33];
  std::snprintf(out, sizeof(out), "%016llx%016llx",
                static_cast<unsigned long long>(hi),
                static_cast<unsigned long long>(lo));
  return std::string(out, 32);
}

bool IsValidTier(QualityTier tier) {
  return tier == QualityTier::kLow || tier == QualityTier::kMedium ||
         tier == QualityTier::kHigh || tier == QualityTier::kUltra;
}

const char* TierNameFor(QualityTier tier) {
  switch (tier) {
    case QualityTier::kLow:
      return "LOW";
    case QualityTier::kMedium:
      return "MEDIUM";
    case QualityTier::kHigh:
      return "HIGH";
    case QualityTier::kUltra:
      return "ULTRA";
  }
  return "UNKNOWN";
}

std::uint64_t& SequenceCounter() {
  static std::uint64_t seq = 0;
  return seq;
}

std::uint64_t& TickCounter() {
  static std::uint64_t tick = 0;
  return tick;
}

bool& OverridePinned() {
  static bool pinned = false;
  return pinned;
}

TelemetryRecord& LastRecordStorage() {
  static TelemetryRecord last;
  return last;
}

TelemetryRecord EmitRecord(QualityTier tier, const char* reason_cstr) {
  const char* name = TierNameFor(tier);
  TelemetryRecord record;
  record.tier = tier;
  record.tierName = name;
  record.reason = reason_cstr;
  record.fingerprintHash = FingerprintFor(name, reason_cstr);
  record.sequence = ++SequenceCounter();
  record.timestampMs = ++TickCounter();
  record.status = Status::kOk;
  LastRecordStorage() = record;
  return record;
}

}  // namespace

TelemetryRecord reportTier(QualityTier current, const char* reason) {
  if (!IsValidTier(current)) {
    TelemetryRecord rejected;
    rejected.tier = current;
    rejected.tierName = "UNKNOWN";
    rejected.reason = reason != nullptr ? reason : "";
    rejected.status = Status::kInvalidTier;
    rejected.sequence = SequenceCounter();
    rejected.timestampMs = TickCounter();
    return rejected;
  }
  if (reason == nullptr) {
    TelemetryRecord rejected;
    rejected.tier = current;
    rejected.tierName = TierNameFor(current);
    rejected.status = Status::kInvalidArg;
    rejected.sequence = SequenceCounter();
    rejected.timestampMs = TickCounter();
    return rejected;
  }
  return EmitRecord(current, reason);
}

Status overrideTier(QualityTier app, QualityTier* active) {
  if (active == nullptr) {
    return Status::kInvalidArg;
  }
  if (!IsValidTier(app)) {
    return Status::kInvalidTier;
  }
  *active = app;
  OverridePinned() = true;
  EmitRecord(app, "app-override");
  return Status::kOk;
}

void clearOverride() { OverridePinned() = false; }

TelemetryRecord lastRecord() { return LastRecordStorage(); }

}  // namespace quality_telemetry

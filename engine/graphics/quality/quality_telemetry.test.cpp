// T7 hermetic unit test — quality-tier telemetry + app override
// (NFR-GRAPHICS-005, AC-GRAPHICS-027, TC-027).
// Style: plain asserts, no dependencies, no I/O, no network, no wall clock.
// Fingerprint hash follows the `fingerprint_hash_for` convention from
// `caps/caps_tier.h` (deterministic FNV-1a hex digest); caps headers are NOT
// included here — tier/selection inputs stay plain mocked data per T6.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "engine/graphics/quality/quality_telemetry.h"

namespace {

using quality_telemetry::QualityTier;
using quality_telemetry::Status;
using quality_telemetry::TelemetryRecord;

bool IsLowerHex32(const std::string& s) {
  if (s.size() != 32u) return false;
  for (char c : s) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

bool SameRecord(const TelemetryRecord& a, const TelemetryRecord& b) {
  return a.tier == b.tier && a.status == b.status &&
         std::strcmp(a.tierName, b.tierName) == 0 && a.reason == b.reason &&
         a.fingerprintHash == b.fingerprintHash && a.sequence == b.sequence &&
         a.timestampMs == b.timestampMs;
}

}  // namespace

int main() {
  // 1. Every tier reports its name + reason with a well-formed fingerprint.
  {
    const QualityTier tiers[] = {QualityTier::kLow, QualityTier::kMedium,
                                 QualityTier::kHigh, QualityTier::kUltra};
    const char* names[] = {"LOW", "MEDIUM", "HIGH", "ULTRA"};
    for (int i = 0; i < 4; ++i) {
      TelemetryRecord r =
          quality_telemetry::reportTier(tiers[i], "policy-select");
      assert(r.status == Status::kOk);
      assert(r.tier == tiers[i]);
      assert(std::strcmp(r.tierName, names[i]) == 0);
      assert(r.reason == "policy-select");
      assert(IsLowerHex32(r.fingerprintHash));
    }
    printf("PASS each-tier-reports-name-reason\n");
  }
  // 2. Fingerprint is stable for the same (tier, reason); sequence advances.
  {
    TelemetryRecord a = quality_telemetry::reportTier(QualityTier::kHigh, "r1");
    TelemetryRecord b = quality_telemetry::reportTier(QualityTier::kHigh, "r1");
    assert(a.status == Status::kOk && b.status == Status::kOk);
    assert(a.fingerprintHash == b.fingerprintHash);
    assert(b.sequence == a.sequence + 1u);
    printf("PASS fingerprint-stable-same-input\n");
  }
  // 3. Fingerprint is distinct across tiers for the same reason.
  {
    TelemetryRecord rLow =
        quality_telemetry::reportTier(QualityTier::kLow, "same");
    TelemetryRecord rMed =
        quality_telemetry::reportTier(QualityTier::kMedium, "same");
    TelemetryRecord rHigh =
        quality_telemetry::reportTier(QualityTier::kHigh, "same");
    TelemetryRecord rUltra =
        quality_telemetry::reportTier(QualityTier::kUltra, "same");
    assert(rLow.fingerprintHash != rMed.fingerprintHash);
    assert(rLow.fingerprintHash != rHigh.fingerprintHash);
    assert(rLow.fingerprintHash != rUltra.fingerprintHash);
    assert(rMed.fingerprintHash != rHigh.fingerprintHash);
    assert(rMed.fingerprintHash != rUltra.fingerprintHash);
    assert(rHigh.fingerprintHash != rUltra.fingerprintHash);
    printf("PASS fingerprint-distinct-across-tiers\n");
  }
  // 4. Sequence is strictly monotonic across report/override interleavings.
  {
    TelemetryRecord r1 =
        quality_telemetry::reportTier(QualityTier::kLow, "seq");
    QualityTier active = QualityTier::kLow;
    assert(quality_telemetry::overrideTier(QualityTier::kHigh, &active) ==
           Status::kOk);
    TelemetryRecord ov = quality_telemetry::lastRecord();
    TelemetryRecord r2 =
        quality_telemetry::reportTier(active, "after-override");
    assert(ov.sequence == r1.sequence + 1u);
    assert(r2.sequence == ov.sequence + 1u);
    assert(r1.sequence < ov.sequence && ov.sequence < r2.sequence);
    printf("PASS sequence-monotonic-across-interleavings\n");
  }
  // 5. Override pins the active tier and is itself recorded.
  {
    QualityTier active = QualityTier::kLow;
    assert(quality_telemetry::overrideTier(QualityTier::kUltra, &active) ==
           Status::kOk);
    assert(active == QualityTier::kUltra);
    TelemetryRecord ov = quality_telemetry::lastRecord();
    assert(ov.tier == QualityTier::kUltra);
    assert(std::strcmp(ov.tierName, "ULTRA") == 0);
    assert(ov.reason == "app-override");
    assert(IsLowerHex32(ov.fingerprintHash));
    quality_telemetry::clearOverride();
    printf("PASS override-pins-active-and-recorded\n");
  }
  // 6. Null active pointer is rejected fail-closed (kInvalidArg, no record).
  {
    TelemetryRecord before = quality_telemetry::lastRecord();
    assert(quality_telemetry::overrideTier(QualityTier::kHigh, nullptr) ==
           Status::kInvalidArg);
    assert(SameRecord(quality_telemetry::lastRecord(), before));
    printf("PASS null-active-rejected-fail-closed\n");
  }
  // 7. Null reason is rejected fail-closed (kInvalidArg, sequence unchanged).
  {
    TelemetryRecord before = quality_telemetry::lastRecord();
    TelemetryRecord r =
        quality_telemetry::reportTier(QualityTier::kHigh, nullptr);
    assert(r.status == Status::kInvalidArg);
    assert(SameRecord(quality_telemetry::lastRecord(), before));
    TelemetryRecord after =
        quality_telemetry::reportTier(QualityTier::kHigh, "ok");
    assert(after.status == Status::kOk);
    assert(after.sequence == before.sequence + 1u);
    printf("PASS null-reason-rejected-fail-closed\n");
  }
  // 8. Override with an invalid tier is rejected, active unchanged.
  {
    QualityTier active = QualityTier::kMedium;
    TelemetryRecord before = quality_telemetry::lastRecord();
    assert(quality_telemetry::overrideTier(static_cast<QualityTier>(99),
                                           &active) != Status::kOk);
    assert(active == QualityTier::kMedium);
    assert(SameRecord(quality_telemetry::lastRecord(), before));
    printf("PASS invalid-tier-override-rejected\n");
  }
  // 9. Report with an invalid tier is rejected, nothing recorded.
  {
    TelemetryRecord before = quality_telemetry::lastRecord();
    TelemetryRecord r = quality_telemetry::reportTier(
        static_cast<QualityTier>(99), "bogus");
    assert(r.status != Status::kOk);
    assert(SameRecord(quality_telemetry::lastRecord(), before));
    printf("PASS invalid-tier-report-rejected\n");
  }
  // 10. clearOverride restores policy selection observability.
  {
    QualityTier active = QualityTier::kLow;
    assert(quality_telemetry::overrideTier(QualityTier::kHigh, &active) ==
           Status::kOk);
    assert(active == QualityTier::kHigh);
    quality_telemetry::clearOverride();
    TelemetryRecord r =
        quality_telemetry::reportTier(QualityTier::kLow, "policy");
    assert(r.status == Status::kOk);
    assert(r.tier == QualityTier::kLow);
    assert(std::strcmp(r.tierName, "LOW") == 0);
    printf("PASS clear-override-restores-policy\n");
  }
  // 11. Timestamps are monotonic (in-process tick, no wall clock).
  {
    TelemetryRecord a = quality_telemetry::reportTier(QualityTier::kLow, "t");
    TelemetryRecord b = quality_telemetry::reportTier(QualityTier::kHigh, "t");
    assert(b.timestampMs >= a.timestampMs);
    printf("PASS timestamps-monotonic\n");
  }
  printf("ALL QUALITY TELEMETRY TESTS PASSED\n");
  return 0;
}

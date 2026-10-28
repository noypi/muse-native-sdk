// T3 hermetic unit test — probed texture-format selection (FR-GRAPHICS-008, AC-GRAPHICS-020/022).
// Style: plain asserts, no dependencies, no I/O, no network.
// Pure function of (DeviceProbe, AssetEncodings); no VkImageFormatProperties calls.
#include <cassert>
#include <cstdio>
#include <cstring>

#include "engine/graphics/textures/texture_format.h"

static DeviceProbe Probe(bool astc, bool etc2, bool bcEmu) {
  DeviceProbe p;
  p.astc = astc;
  p.etc2 = etc2;
  p.bcEmulator = bcEmu;
  return p;
}

static AssetEncodings Assets(bool ktx2Astc, bool ktx2Etc2, bool bc) {
  AssetEncodings a;
  a.ktx2Astc = ktx2Astc;
  a.ktx2Etc2 = ktx2Etc2;
  a.bc = bc;
  return a;
}

int main() {
  // 1. ASTC-capable device + ASTC asset -> kAstc (highest priority).
  {
    FormatSelection s = selectTextureFormat(Probe(true, true, false), Assets(true, true, false));
    assert(s.format == GpuFormat::kAstc);
    assert(s.reason != nullptr && std::strlen(s.reason) > 0);
    printf("PASS astc-device-astc-asset-selects-astc\n");
  }
  // 2. Priority: ASTC beats ETC2 when both probe and asset offer both.
  {
    FormatSelection s = selectTextureFormat(Probe(true, true, false), Assets(true, true, true));
    assert(s.format == GpuFormat::kAstc);
    printf("PASS astc-beats-etc2\n");
  }
  // 3. ETC2-only device -> kEtc2 even when the asset also carries ASTC.
  {
    FormatSelection s = selectTextureFormat(Probe(false, true, false), Assets(true, true, false));
    assert(s.format == GpuFormat::kEtc2);
    printf("PASS etc2-only-device-selects-etc2-despite-astc-asset\n");
  }
  // 4. No-compression device -> kUncompressed regardless of asset encodings.
  {
    FormatSelection s = selectTextureFormat(Probe(false, false, false), Assets(true, true, true));
    assert(s.format == GpuFormat::kUncompressed);
    assert(s.transcoded == false);
    printf("PASS no-compression-device-selects-uncompressed\n");
  }
  // 5. BC selected only with bcEmulator==true and no ASTC/ETC2 support.
  {
    FormatSelection s = selectTextureFormat(Probe(false, false, true), Assets(false, false, true));
    assert(s.format == GpuFormat::kBc);
    printf("PASS bc-emulator-fallback-selects-bc\n");
  }
  // 6. BC never selected when bcEmulator==false (falls through to uncompressed).
  {
    FormatSelection s = selectTextureFormat(Probe(false, false, false), Assets(false, false, true));
    assert(s.format == GpuFormat::kUncompressed);
    printf("PASS bc-rejected-without-emulator-flag\n");
  }
  // 7. BC never preempts ETC2 even on an emulator-flagged device.
  {
    FormatSelection s = selectTextureFormat(Probe(false, true, true), Assets(true, true, true));
    assert(s.format == GpuFormat::kEtc2);
    printf("PASS etc2-beats-bc-on-emulator\n");
  }
  // 8. Probe never violated: ASTC-capable probe but asset without ASTC
  //    encoding falls to the next supported+encoded format (ETC2).
  {
    FormatSelection s = selectTextureFormat(Probe(true, true, false), Assets(false, true, false));
    assert(s.format == GpuFormat::kEtc2);
    printf("PASS missing-asset-encoding-falls-through\n");
  }
  // 9. Probe never violated: ASTC-only probe, ETC2-only asset -> uncompressed
  //    (must not select ETC2 the probe does not advertise).
  {
    FormatSelection s = selectTextureFormat(Probe(true, false, false), Assets(false, true, false));
    assert(s.format == GpuFormat::kUncompressed);
    printf("PASS probe-without-asset-match-selects-uncompressed\n");
  }
  // 10. Selection is a pure function of the probe (no compile-time
  //     assumption): identical inputs give identical outputs.
  {
    DeviceProbe p = Probe(true, true, false);
    AssetEncodings a = Assets(true, true, false);
    FormatSelection s1 = selectTextureFormat(p, a);
    FormatSelection s2 = selectTextureFormat(p, a);
    assert(s1.format == s2.format);
    assert(s1.transcoded == s2.transcoded);
    printf("PASS selection-deterministic-pure-function\n");
  }
  // 11. Transcoded flag: compressed selections from KTX2 encodings report
  //     transcoded=true; uncompressed fallback reports false.
  {
    FormatSelection s = selectTextureFormat(Probe(true, false, false), Assets(true, false, false));
    assert(s.format == GpuFormat::kAstc);
    assert(s.transcoded == true);
    FormatSelection u = selectTextureFormat(Probe(false, false, false), Assets(false, false, false));
    assert(u.format == GpuFormat::kUncompressed);
    assert(u.transcoded == false);
    printf("PASS transcoded-flag-tracks-compressed-selection\n");
  }
  // 12. Reason string present on every path, including the BC path.
  {
    FormatSelection s = selectTextureFormat(Probe(false, false, true), Assets(false, false, true));
    assert(s.reason != nullptr && std::strlen(s.reason) > 0);
    printf("PASS reason-present-on-bc-path\n");
  }
  printf("ALL T3 TESTS PASSED\n");
  return 0;
}

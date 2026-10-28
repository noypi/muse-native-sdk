// Probed texture-format selection (FR-GRAPHICS-008).
// Pure logic, no Vulkan calls.
#include "engine/graphics/textures/texture_format.h"

FormatSelection selectTextureFormat(const DeviceProbe& probe,
                                    const AssetEncodings& asset) {
  FormatSelection out;
  if (probe.astc && asset.ktx2Astc) {
    out.format = GpuFormat::kAstc;
    out.transcoded = true;
    out.reason = "astc: probed support with matching ktx2 encoding";
    return out;
  }
  if (probe.etc2 && asset.ktx2Etc2) {
    out.format = GpuFormat::kEtc2;
    out.transcoded = true;
    out.reason = "etc2: probed support with matching ktx2 encoding";
    return out;
  }
  if (probe.bcEmulator && asset.bc) {
    out.format = GpuFormat::kBc;
    out.transcoded = true;
    out.reason = "bc: emulator fallback, no astc/etc2 selectable";
    return out;
  }
  out.format = GpuFormat::kUncompressed;
  out.transcoded = false;
  out.reason = "uncompressed: no probed compressed format with asset encoding";
  return out;
}

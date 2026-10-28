// Probed ASTC/ETC2 (+BC emulator fallback) texture-format selection (FR-GRAPHICS-008).
// Pure logic, no Vulkan VkImageFormatProperties calls: the caller passes the
// probed capabilities via DeviceProbe and the available transcode inputs via
// AssetEncodings; selection never picks a format the probe does not advertise.
#ifndef ENGINE_GRAPHICS_TEXTURES_TEXTURE_FORMAT_H_
#define ENGINE_GRAPHICS_TEXTURES_TEXTURE_FORMAT_H_

enum class GpuFormat { kAstc, kEtc2, kBc, kUncompressed };

struct DeviceProbe {
  bool astc = false;
  bool etc2 = false;
  bool bcEmulator = false;
};

struct AssetEncodings {
  bool ktx2Astc = false;
  bool ktx2Etc2 = false;
  bool bc = false;
};

struct FormatSelection {
  GpuFormat format = GpuFormat::kUncompressed;
  bool transcoded = false;
  const char* reason = "";
};

// Selects the highest-priority supported format with a matching asset encoding.
// Priority: ASTC > ETC2 > BC (emulator only) > uncompressed. A format is
// selected only when the probe advertises it AND the asset carries the
// matching encoding; otherwise selection falls through to the next priority.
// BC is selected only when bcEmulator==true, the asset carries BC, and neither
// ASTC nor ETC2 was selectable. Deterministic pure function of its inputs.
FormatSelection selectTextureFormat(const DeviceProbe& probe,
                                    const AssetEncodings& asset);

#endif  // ENGINE_GRAPHICS_TEXTURES_TEXTURE_FORMAT_H_

#pragma once

// FR-MEDIA-014 lane-first registry-handle lane (AC-MEDIA-022/023/024).
// `media::LaneRegistry` maps an image `source` to a stable nonzero `u32`
// handle: first resolve decodes once (admitted to both tiers), repeat
// resolves are memory hits, `evictMemory()` drops the memory index while the
// disk index repromotes without re-decode. Unresolvable sources (empty or
// oversized-hostile) yield 0 and never throw.
//
// `media::DrawCmd` is the lane carriage: `payload.assetId` must survive value
// copies end-to-end. `media::Swapchain::lookup` resolves a handle to its
// pixel and fails closed to the current solid fill for 0/unknown handles
// (never blank, never transparent, never throws). Texture upload / VkImage
// import / RGBA blit stay graphics-owned per CONFLICT-MEDIA-003 — this unit
// is carriage-to-handle only.
//
// Hermetic: C++17, no network/env/filesystem/live-service deps.

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace media {

using AssetId = std::uint32_t;

constexpr AssetId kUnresolvedAsset = 0;

struct Pixel {
  std::uint8_t r, g, b, a;
  bool operator==(const Pixel& o) const {
    return r == o.r && g == o.g && b == o.b && a == o.a;
  }
  bool operator!=(const Pixel& o) const { return !(*this == o); }
};

// Current solid fill rendered for assetId == 0. Distinct from blank
// (0x00000000) and transparent.
constexpr Pixel kSolidFill{0x2A, 0x7D, 0xC4, 0xFF};

// Lane carriage: payload.assetId must survive value copies.
struct DrawCmd {
  struct Payload {
    AssetId assetId = kUnresolvedAsset;
  };
  Payload payload;
};

class LaneRegistry {
 public:
  LaneRegistry();

  // Incremented exactly once per actual decode (first resolve per source).
  std::size_t decodeCount = 0;
  std::size_t memoryHits = 0;
  std::size_t diskHits = 0;

  // Resolves a source to a stable nonzero handle; 0 == unresolvable.
  // Never throws.
  AssetId resolve(const std::string& source) noexcept;

  // Drops the memory index, keeps the disk index. Never throws.
  void evictMemory() noexcept;

 private:
  std::unordered_map<std::string, AssetId> memory_;
  std::unordered_map<std::string, AssetId> disk_;
  AssetId nextHandle_;
};

class Swapchain {
 public:
  Swapchain();

  // Nonzero handle -> its pixel; 0/unknown -> current solid fill.
  // Never throws.
  Pixel lookup(AssetId id) const noexcept;
  void admit(AssetId id, Pixel p);

 private:
  std::unordered_map<AssetId, Pixel> pixels_;
};

}  // namespace media

// FR-MEDIA-014 implementation — see engine/media/include/media/lane_registry.h.
#include "engine/media/include/media/lane_registry.h"

namespace media {

LaneRegistry::LaneRegistry() : nextHandle_(1) {}  // 0 is reserved

AssetId LaneRegistry::resolve(const std::string& source) noexcept {
  try {
    if (source.empty() || source.size() > 4096) return kUnresolvedAsset;
    const auto mem = memory_.find(source);
    if (mem != memory_.end()) {
      ++memoryHits;
      return mem->second;
    }
    const auto disk = disk_.find(source);
    if (disk != disk_.end()) {
      ++diskHits;
      memory_[source] = disk->second;  // repromote without re-decode
      return disk->second;
    }
    const AssetId h = nextHandle_++;
    memory_[source] = h;
    disk_[source] = h;
    ++decodeCount;
    return h;
  } catch (...) {
    return kUnresolvedAsset;
  }
}

void LaneRegistry::evictMemory() noexcept {
  try {
    memory_.clear();
  } catch (...) {
  }
}

Swapchain::Swapchain() = default;

Pixel Swapchain::lookup(AssetId id) const noexcept {
  if (id == kUnresolvedAsset) return kSolidFill;
  const auto it = pixels_.find(id);
  if (it != pixels_.end()) return it->second;
  return kSolidFill;  // unknown handle fails closed too
}

void Swapchain::admit(AssetId id, Pixel p) { pixels_[id] = p; }

}  // namespace media

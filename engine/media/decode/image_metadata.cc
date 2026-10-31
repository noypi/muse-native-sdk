#include "engine/media/decode/image_metadata.h"

namespace media {

// T1 stub: always returns an invalid (safe-default) descriptor.
// Batch 2 implements real header parsing.
ImageMetadata parseImageMetadata(const uint8_t* bytes, std::size_t len) {
  (void)bytes;
  (void)len;
  return ImageMetadata{};
}

}  // namespace media

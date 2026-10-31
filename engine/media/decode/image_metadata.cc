#include "engine/media/decode/image_metadata.h"

namespace media {

// T1 stub: always returns an invalid (safe-default) descriptor.
ImageMetadata parseImageMetadata(const uint8_t* bytes, std::size_t len) {
  (void)bytes;
  (void)len;
  return ImageMetadata{};
}

// T3 lives header-inline; T4 (readColorProfile/applyColorAndSize) lives
// header-inline too so decode_dispatch.cc links without this TU.

}  // namespace media

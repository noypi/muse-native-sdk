#pragma once

#include <cstddef>
#include <cstdint>

namespace media {

// T1 stub for the Batch 2 metadata path (FR-MEDIA-001/AC-MEDIA-003).
// Shapes only; parsing stays unimplemented until Batch 2.
struct ImageMetadata {
  uint32_t width = 0;
  uint32_t height = 0;
  bool valid = false;
};

ImageMetadata parseImageMetadata(const uint8_t* bytes, std::size_t len);

}  // namespace media

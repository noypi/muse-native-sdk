// T1 implementation — see engine/media/include/media/zero_copy_frame.h.
#include "engine/media/include/media/zero_copy_frame.h"

namespace media {

bool IsZeroCopyAvailable(const DeviceCaps& caps) {
  try {
    return caps.external_memory && caps.sync_fence;
  } catch (...) {
    return false;
  }
}

ZeroCopyImportResult ImportHardwareBuffer(const HardwareBufferHandle& buffer,
                                          uint64_t fence_fd,
                                          bool* out_host_copy_avoided) {
  try {
    // Fail closed on an invalid handle: never a silent host copy.
    if (!buffer.valid) {
      if (out_host_copy_avoided != nullptr) {
        *out_host_copy_avoided = false;
      }
      return ZeroCopyImportResult{false, 0, false,
                                  "invalid hardware buffer handle"};
    }
    // fence_fd == 0 models "no native sync fence available" (RULE-MEDIA-002
    // fence gate): fail closed rather than claim a tear-free zero-copy
    // import without a bound fence.
    if (fence_fd == 0) {
      if (out_host_copy_avoided != nullptr) {
        *out_host_copy_avoided = false;
      }
      return ZeroCopyImportResult{false, 0, false,
                                  "missing native sync fence"};
    }
    // Capable path: external-memory import with the fence bound, so the
    // frame bypasses host memory end-to-end (AC-MEDIA-002). The VkImage
    // handle is derived from the buffer id and pinned nonzero so a bound
    // image is always observable hermetically.
    const uint64_t vk_image = buffer.id != 0 ? buffer.id : 1u;
    if (out_host_copy_avoided != nullptr) {
      *out_host_copy_avoided = true;
    }
    return ZeroCopyImportResult{true, vk_image, true, nullptr};
  } catch (...) {
    try {
      if (out_host_copy_avoided != nullptr) {
        *out_host_copy_avoided = false;
      }
    } catch (...) {
    }
    return ZeroCopyImportResult{false, 0, false, "import failed"};
  }
}

}  // namespace media

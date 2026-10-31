#pragma once

// Zero-copy frame import contract (NFR-MEDIA-002/AC-MEDIA-002;
// required FR-MEDIA-005/AC-MEDIA-008, FR-MEDIA-006/AC-MEDIA-009;
// RULE-MEDIA-002).
//
// Hermetic: C++17, no network/env/filesystem/live-service deps.
// AHardwareBuffer/VkImage import and the native sync fence are modeled as
// narrow injected shapes (HardwareBufferHandle + fence_fd); the platform
// capability gate (VK_ANDROID_external_memory_android_hardware_buffer +
// native sync fence) is modeled by DeviceCaps. Never throws.

#include <cstdint>

namespace media {

// Narrow injected handle for an AHardwareBuffer peer object.
struct HardwareBufferHandle {
  bool valid = false;
  uint64_t id = 0;
};

// Capability gate seam (RULE-MEDIA-002): both halves required.
struct DeviceCaps {
  bool external_memory = false;
  bool sync_fence = false;
};

struct ZeroCopyImportResult {
  bool ok = false;
  uint64_t vk_image = 0;
  bool fence_ok = false;
  const char* reason = nullptr;
};

// Capability query: true only when both external-memory import and
// sync-fence support are present. Never throws.
bool IsZeroCopyAvailable(const DeviceCaps& caps);

// Imports `buffer` as a VkImage with `fence_fd` bound.
//  - capable (valid buffer, fence_fd != 0) -> {ok=true, vk_image != 0,
//    fence_ok=true}, *out_host_copy_avoided=true.
//  - incapable (invalid buffer or fence_fd == 0 meaning "no native sync
//    fence") -> {ok=false, reason} fail-closed, *out_host_copy_avoided=false.
//    No silent fallback to a host copy; success never claims
//    host_copy_avoided without a bound fence.
//  - out_host_copy_avoided == nullptr is tolerated (no crash).
// Never throws.
ZeroCopyImportResult ImportHardwareBuffer(const HardwareBufferHandle& buffer,
                                          uint64_t fence_fd,
                                          bool* out_host_copy_avoided);

}  // namespace media

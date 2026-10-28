#pragma once

#include <cstddef>

// NFR-CORE-007 budget manager seam (honest coverage of the Rationale/Notes
// prose only: NFR-CORE-007 ships with no ACs and no TCs in
// manual-context.local/specs-core-engine.md, so nothing here claims
// acceptance-criteria conformance).
//
// Adaptive VRAM tiers (from the NFR-CORE-007 prose):
//   - Device RAM < 4 GiB  -> VRAM cap within [32, 48] MiB. Low-end devices
//     are expected to evict aggressively (see onTrim mapping below).
//   - Device RAM >= 6 GiB -> VRAM cap >= 128 MiB. High-end devices may
//     additionally prefetch; the cap is a floor, not a ceiling.
//   - Device RAM in [4 GiB, 6 GiB) interpolates linearly between the two
//     documented tier bounds: cap(ram) = 48 MiB + (ram - 4 GiB) *
//     (128 MiB - 48 MiB) / (6 GiB - 4 GiB), i.e. 48 MiB at 4 GiB rising to
//     128 MiB at 6 GiB. The curve is monotone non-decreasing and bounded by
//     the documented tiers; it is the implementor's documented choice for
//     the otherwise-unspecified mid band.
//
// onTrimMemory hook: onTrim(int) maps ComponentCallbacks2-style trim levels
// (higher = more critical) to eviction aggressiveness:
//   - level < 20 (e.g. TRIM_MEMORY_UI_HIDDEN and below): hint only, no
//     eviction; accounting is untouched.
//   - 20 <= level < 60: moderate pressure, evicts half of current usage.
//   - level >= 60 (MODERATE/COMPLETE-class, e.g. 80): critical pressure,
//     evicts everything (usage drops to zero).
//
// VMA-facing caps: acquireVram/releaseVram perform in-process byte
// accounting against vramCapBytes only; there is no Vulkan/VMA allocator
// binding here. Real GPU residency via VMA is a device-integration concern
// and is intentionally not wired in this hermetic host-side seam.
//
// Crashpad/minidump wiring is explicitly excluded (device concern); a
// follow-up device run owns it.
//
// Threading: this seam is NOT thread-safe; callers serialize access. All
// entry points are noexcept so budget pressure can never throw on the
// render path. releaseVram clamps saturatingly (releasing more than held
// resets usage to zero, never wraps).
namespace core {
namespace budget {

using std::size_t;

class Manager {
 public:
  struct Config {
    size_t ramBytes;
    size_t vramCapBytes;
  };

  explicit Manager(Config c) noexcept;

  Manager(const Manager&) = delete;
  Manager& operator=(const Manager&) = delete;

  // Tier table described above. Echoes ramBytes in the returned Config.
  static Config tierForRam(size_t ramBytes) noexcept;

  // Evict per the trim-level mapping above.
  void onTrim(int level) noexcept;

  // Admit bytes when 0 < bytes <= remaining cap; otherwise reject without
  // changing usage. Never throws, never falls back to the heap.
  bool acquireVram(size_t bytes) noexcept;

  // Return bytes to the budget, clamping saturatingly at zero.
  void releaseVram(size_t bytes) noexcept;

  size_t used() const noexcept { return used_; }
  size_t cap() const noexcept { return config_.vramCapBytes; }

 private:
  Config config_;
  size_t used_;
};

// RAII acquire/release wrapper: acquires bytes on construction (holding
// them while alive), releases on destruction — including during exception
// unwind. Non-copyable; non-movable (owns a live reservation).
class VramGuard {
 public:
  VramGuard(Manager& mgr, size_t bytes) noexcept;
  ~VramGuard() noexcept;

  VramGuard(const VramGuard&) = delete;
  VramGuard& operator=(const VramGuard&) = delete;

 private:
  Manager& mgr_;
  size_t bytes_;
  bool held_;
};

}  // namespace budget
}  // namespace core

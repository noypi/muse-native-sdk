#include "core/budget/manager.h"

#include <cstdint>

namespace core {
namespace budget {

namespace {
const size_t kMiB = 1024u * 1024u;
const size_t kGiB = 1024u * 1024u * 1024u;
const size_t kLowCapBytes = 48 * kMiB;
const size_t kHighCapBytes = 128 * kMiB;
const size_t kLowRamBytes = 4 * kGiB;
const size_t kHighRamBytes = 6 * kGiB;
}  // namespace

Manager::Manager(Config c) noexcept : config_(c), used_(0) {}

Manager::Config Manager::tierForRam(size_t ramBytes) noexcept {
  if (ramBytes < kLowRamBytes) {
    return Config{ramBytes, kLowCapBytes};
  }
  if (ramBytes >= kHighRamBytes) {
    return Config{ramBytes, kHighCapBytes};
  }
  // Linear interpolation between the documented tier bounds (see header).
  const size_t span = kHighRamBytes - kLowRamBytes;
  const size_t growth = kHighCapBytes - kLowCapBytes;
  const size_t off = ramBytes - kLowRamBytes;
  // Compute in 64 bits so off * growth cannot wrap on 32-bit size_t.
  const uint64_t cap =
      static_cast<uint64_t>(kLowCapBytes) +
      static_cast<uint64_t>(off) * static_cast<uint64_t>(growth) /
          static_cast<uint64_t>(span);
  return Config{ramBytes, static_cast<size_t>(cap)};
}

void Manager::onTrim(int level) noexcept {
  if (level < 20) {
    return;
  }
  if (level < 60) {
    used_ -= used_ / 2;
    return;
  }
  used_ = 0;
}

bool Manager::acquireVram(size_t bytes) noexcept {
  if (bytes == 0) {
    return false;
  }
  if (bytes > config_.vramCapBytes) {
    return false;
  }
  if (used_ >= config_.vramCapBytes) {
    return false;
  }
  if (bytes > config_.vramCapBytes - used_) {
    return false;
  }
  used_ += bytes;
  return true;
}

void Manager::releaseVram(size_t bytes) noexcept {
  if (bytes >= used_) {
    used_ = 0;
    return;
  }
  used_ -= bytes;
}

VramGuard::VramGuard(Manager& mgr, size_t bytes) noexcept
    : mgr_(mgr), bytes_(bytes), held_(mgr_.acquireVram(bytes)) {}

VramGuard::~VramGuard() noexcept {
  if (held_) {
    mgr_.releaseVram(bytes_);
  }
}

}  // namespace budget
}  // namespace core

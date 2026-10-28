#include "core/budget/manager.h"

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
  // off < span and growth < 2^32, so off * growth cannot overflow size_t.
  return Config{ramBytes, kLowCapBytes + off * growth / span};
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
  if (used_ + bytes > config_.vramCapBytes) {
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

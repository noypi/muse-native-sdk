#include "motion/asset_prepare.h"

#include <array>
#include <mutex>
#include <thread>

namespace {

int formatIndex(AssetFormat f) {
  switch (f) {
    case AssetFormat::kLottie:
      return 0;
    case AssetFormat::kRive:
      return 1;
    case AssetFormat::kAvd:
      return 2;
    case AssetFormat::kSvg:
      return 3;
  }
  return 0;
}

bool isDecodableByte(uint8_t b) {
  return b == 0x09 || b == 0x0A || b == 0x0D || (b >= 0x20 && b <= 0x7E);
}

std::array<bool, 4>& preparedFlags() {
  static std::array<bool, 4> flags = {false, false, false, false};
  return flags;
}

std::mutex& prepareMutex() {
  static std::mutex m;
  return m;
}

std::function<void()>& threadHook() {
  static std::function<void()> h;
  return h;
}

std::mutex& hookMutex() {
  static std::mutex m;
  return m;
}

}  // namespace

void motion_setThreadHook(std::function<void()> h) {
  try {
    std::lock_guard<std::mutex> l(hookMutex());
    threadHook() = std::move(h);
  } catch (...) {
  }
}

bool motion_isPrepared(AssetFormat f) {
  try {
    std::lock_guard<std::mutex> l(prepareMutex());
    return preparedFlags()[static_cast<size_t>(formatIndex(f))];
  } catch (...) {
    return false;
  }
}

void motion_prepareAsync(AssetFormat f, std::vector<uint8_t> bytes,
                         AssetReady cb) {
  try {
    std::thread([f, bytes = std::move(bytes), cb = std::move(cb)]() mutable {
      AssetError result = AssetError::kOk;
      try {
        // Observe decode on the worker thread.
        std::function<void()> hook;
        try {
          std::lock_guard<std::mutex> l(hookMutex());
          hook = threadHook();
        } catch (...) {
        }
        if (hook) {
          try {
            hook();
          } catch (...) {
          }
        }
        if (bytes.empty()) {
          result = AssetError::kUnsupported;
        } else {
          for (uint8_t b : bytes) {
            if (!isDecodableByte(b)) {
              result = AssetError::kDecodeFailed;
              break;
            }
          }
        }
        if (result == AssetError::kOk) {
          try {
            std::lock_guard<std::mutex> l(prepareMutex());
            preparedFlags()[static_cast<size_t>(formatIndex(f))] = true;
          } catch (...) {
          }
        }
      } catch (...) {
        result = AssetError::kDecodeFailed;
      }
      if (cb) {
        try {
          cb(result);
        } catch (...) {
        }
      }
    }).detach();
  } catch (...) {
  }
}

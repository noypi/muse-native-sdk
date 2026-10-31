// T5 implementation — see engine/media/include/media/fetch_bridge.h.
#include "engine/media/include/media/fetch_bridge.h"

#include <limits>
#include <memory>
#include <unordered_map>

#include "core/budget/manager.h"
#include "engine/net/include/net/types.h"

namespace media {
namespace {

// Admission estimate in bytes. Kept at 1 so the seam refuses exactly when
// the cap is 0 (the hermetic `setBudgetBytes(0)` refusal shape from T1).
constexpr std::size_t kAdmitEstimateBytes = 1;

// Synthetic payload length for the hermetic net stand-in.
constexpr std::size_t kSyntheticBodyBytes = 64;

// Process-wide budget manager for the glue. Recreated by
// `setFetchBudgetBytes`; defaults to an ample cap.
std::unique_ptr<core::budget::Manager>& fetchManager() {
  static std::unique_ptr<core::budget::Manager> mgr(
      new core::budget::Manager(core::budget::Manager::Config{
          8u * 1024u * 1024u * 1024u,
          (std::numeric_limits<std::size_t>::max)()}));
  return mgr;
}

// Process-wide fetched-URL index (net-vs-cache origin for `fromNet`).
std::unordered_map<std::string, std::vector<uint8_t>>& fetchCache() {
  static std::unordered_map<std::string, std::vector<uint8_t>> cache;
  return cache;
}

// Deterministic hermetic stand-in for the transport body: FNV-1a over the
// request URL, expanded to `kSyntheticBodyBytes`. No sockets are opened.
std::vector<uint8_t> synthesizeBody(const muse::net::Request& req) {
  uint64_t h = 1469598103934665603ull;
  for (unsigned char c : req.url) {
    h ^= c;
    h *= 1099511628211ull;
  }
  std::vector<uint8_t> out;
  out.reserve(kSyntheticBodyBytes);
  for (std::size_t i = 0; i < kSyntheticBodyBytes; ++i) {
    out.push_back(static_cast<uint8_t>((h >> ((i % 8) * 8)) & 0xffu));
    h *= 1099511628211ull;
  }
  return out;
}

}  // namespace

void setFetchBudgetBytes(std::size_t bytes) noexcept {
  try {
    fetchManager().reset(new core::budget::Manager(
        core::budget::Manager::Config{8u * 1024u * 1024u * 1024u, bytes}));
  } catch (...) {
  }
}

void clearFetchCache() {
  try {
    fetchCache().clear();
  } catch (...) {
  }
}

FetchResult fetchImageBytes(std::string url) {
  try {
    if (url.empty()) {
      return FetchResult{{}, false, Status::kInvalidArg};
    }
    // Net HTTP seam: the request is shaped as a `muse::net` GET; the
    // hermetic build answers it synthetically (never a private socket).
    const muse::net::Request req{"GET", url, {}, {}};
    // Budget admission through `core::budget::Manager`; refusal surfaces
    // kOverBudget without fetching. The probe reservation is released
    // immediately so the seam stays side-effect free.
    if (!fetchManager()->acquireVram(kAdmitEstimateBytes)) {
      return FetchResult{{}, false, Status::kOverBudget};
    }
    fetchManager()->releaseVram(kAdmitEstimateBytes);
    auto& cache = fetchCache();
    const auto hit = cache.find(url);
    if (hit != cache.end()) {
      return FetchResult{hit->second, false, Status::kOk};
    }
    std::vector<uint8_t> body = synthesizeBody(req);
    cache.emplace(url, body);
    return FetchResult{body, true, Status::kOk};
  } catch (...) {
    return FetchResult{{}, false, Status::kOverBudget};
  }
}

}  // namespace media

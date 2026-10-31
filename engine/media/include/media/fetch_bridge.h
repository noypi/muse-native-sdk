#pragma once

// T5 — fetch/budget/eviction glue (FR-MEDIA-004/AC-MEDIA-007 integration).
// Thin bridge proving images arrive via the net HTTP seam into the T1 cache
// under budget pressure. Delegates to the `engine/net` HTTP path
// (`muse::net::Request` shape); no private sockets are opened here.
//
// Hermetic: C++17, never throws, no network/env/filesystem/live-service
// deps. The byte payload is synthesized deterministically from the URL
// (hermetic stand-in for the transport body); budget admission is refused
// through `core::budget::Manager::acquireVram`.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/media/include/media/image_cache.h"

namespace media {

// `bytes` = `data` (contract A-TYPES); `Status::{kOk,kOverBudget,
// kInvalidArg}` is the T1-shared `Status` (no second definition).
struct FetchResult {
  std::vector<uint8_t> data;
  bool fromNet = false;
  Status status = Status::kOk;
};

// Fetches `url` through the net HTTP seam into the cache admission path.
//  - empty url -> {empty, fromNet=false, kInvalidArg} without touching net.
//  - budget refusal (`core::budget::Manager` refuses the estimate) ->
//    {empty, fromNet=false, kOverBudget} without fetching.
//  - first fetch of a URL -> {synthetic bytes, fromNet=true, kOk}.
//  - repeat fetch of a URL -> {cached bytes, fromNet=false, kOk}.
// Never throws.
FetchResult fetchImageBytes(std::string url);

// Budget seam for the glue (hermetic stand-in for wiring the real
// `core::budget::Manager` cap): 0 forces kOverBudget refusal. Never throws.
void setFetchBudgetBytes(std::size_t bytes) noexcept;

// Drops the fetched-URL index (memory-eviction analogue). Never throws.
void clearFetchCache();

}  // namespace media

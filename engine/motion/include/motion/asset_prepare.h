#ifndef MOTION_ASSET_PREPARE_H_
#define MOTION_ASSET_PREPARE_H_

#include <cstdint>
#include <functional>
#include <vector>

#include "motion/asset.h"

// Async prepare surface (FR-MOTION-010 / AC-MOTION-032).
//
// motion_prepareAsync decodes the payload for format f on a worker thread
// and invokes cb with the decode result (kOk for a decodable payload,
// kUnsupported for empty input, kDecodeFailed for undecodable bytes).
// Decoding never runs on the calling thread. The callback may be null, in
// which case completion is observable via motion_isPrepared. Nothing here
// throws; oversized or malformed input is reported via AssetError.
//
// motion_isPrepared reports whether any prepare for format f has completed
// with kOk. Prepared state is per-format and sticky (a failed prepare never
// clears a previous success). Composition with the T7 player API: prepare
// state is global per format, while AssetPlayer gating stays per instance;
// after the kOk callback, a fresh player loading the same payload plays kOk.
//
// motion_setThreadHook installs a test hook invoked on the worker thread
// during decode; passing nullptr clears it. Never throws.

using AssetReady = std::function<void(AssetError)>;

void motion_prepareAsync(AssetFormat f, std::vector<uint8_t> bytes,
                         AssetReady cb);
bool motion_isPrepared(AssetFormat f);
void motion_setThreadHook(std::function<void()> h);

#endif  // MOTION_ASSET_PREPARE_H_

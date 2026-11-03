// FR-GRAPHICS-018 game-generic retained draw vocabulary (AC-048/AC-049).
// CPU reference path ships FIRST per spec (Skia may read staged bytes later;
// CPU serves first). Hermetic: C++17 + stdlib only; no device, no network.
// Composition root: standalone test TUs include this header directly; the
// shipped `runtime` graph compiles src/game_vocab.cpp (which includes this
// header plus runtime/renderer.h and static-asserts the kind composition).
//
// Kind composition onto EXISTING ::runtime::PrimitiveKind (no per-game names):
//   kSprite->Image  kSpriteBatch->Image  kTileGrid->Rect  kPolyline->CircleArc
//   kEllipse->CircleArc  kTextRun->ShapedText  kClipRect->Rect
//   kTransformMark->Rect  kOpacityGroup->StateLayer  kRect->Rect
//   kRoundedRect->RoundedRect  kShadow->ElevationShadow  kScrim->OverlayScrim
//   kImage->Image  kIcon->VectorIcon
// Per-game names (kDrawFlappyScene/kDrawFlappyBird/kUnknown) and undeclared
// values are rejected with Status::kUnknownKind + named log. Bounds enforced
// at stage/commit: finite rotation, in-atlas UV, alpha [0,1], capped
// instances, stack depth 8; violations fail closed + counted, never truncated.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace game_vocab {

enum class Status : std::uint32_t {
  kOk = 0,
  kUnknownKind = 1,
  kOverCap = 2,
  kParityMismatch = 3,  // Reserved for T4 Skia parity gate; never emitted here.
};

enum class GameKind : std::uint32_t {
  kUnknown = 0,
  // AC-048 new generic kinds.
  kSprite = 1,
  kSpriteBatch = 2,
  kTileGrid = 3,
  kPolyline = 4,
  kEllipse = 5,
  kTextRun = 6,
  kClipRect = 7,
  kTransformMark = 8,
  kOpacityGroup = 9,
  // Pre-existing composed kinds.
  kRect = 11,
  kRoundedRect = 12,
  kShadow = 13,
  kScrim = 14,
  kImage = 15,
  kIcon = 16,
  // Per-game names: declared only so staging can reject them by name.
  kDrawFlappyScene = 1001,
  kDrawFlappyBird = 1002,
};

struct GameCmd {
  std::uint32_t kindValue = 0;
  float rotationRad = 0.0f;  // Must be finite (T5).
  float u0 = 0.0f;           // In-atlas UV (T5): each in [0,1], u0<=u1, v0<=v1.
  float v0 = 0.0f;
  float u1 = 1.0f;
  float v1 = 1.0f;
  float alpha = 1.0f;  // Must be in [0,1] (T5).
  std::uint32_t instances = 1;  // 1..kMaxBatchInstances (T5).
};

struct StageResult {
  Status status = Status::kOk;
  std::string log;
};

struct StagedFrame {
  std::uint64_t frame = 0;
  std::vector<GameCmd> commands;
  int stackDepth = 0;  // 0..kMaxStackDepth (T5).
};

using FrameBytes = std::vector<std::uint8_t>;

struct CommitResult {
  bool ok = false;
  FrameBytes bytes;
  Status status = Status::kOk;
  std::string log;
};

struct CpuPixel {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 0;
};

constexpr std::uint32_t kMaxBatchInstances = 256;
constexpr int kMaxStackDepth = 8;

inline std::uint64_t g_committedFrames = 0;
inline std::uint64_t g_violations = 0;

inline bool IsAcceptedKindValue(std::uint32_t v) {
  switch (v) {
    case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9:
    case 11: case 12: case 13: case 14: case 15: case 16:
      return true;
    default:
      return false;
  }
}

inline Status ValidatePayload(const GameCmd& cmd, std::string& log) {
  if (!std::isfinite(cmd.rotationRad)) {
    log = "game_vocab: non-finite rotation rejected";
    return Status::kOverCap;
  }
  if (!(cmd.u0 >= 0.0f && cmd.u0 <= 1.0f && cmd.u1 >= 0.0f && cmd.u1 <= 1.0f &&
        cmd.v0 >= 0.0f && cmd.v0 <= 1.0f && cmd.v1 >= 0.0f && cmd.v1 <= 1.0f &&
        cmd.u0 <= cmd.u1 && cmd.v0 <= cmd.v1)) {
    log = "game_vocab: out-of-atlas UV rejected";
    return Status::kOverCap;
  }
  if (!(cmd.alpha >= 0.0f && cmd.alpha <= 1.0f)) {
    log = "game_vocab: alpha outside [0,1] rejected";
    return Status::kOverCap;
  }
  if (cmd.instances == 0 || cmd.instances > kMaxBatchInstances) {
    log = "game_vocab: instance count over cap rejected";
    return Status::kOverCap;
  }
  return Status::kOk;
}

inline StageResult stage(GameCmd cmd) {
  if (!IsAcceptedKindValue(cmd.kindValue)) {
    ++g_violations;
    StageResult r;
    r.status = Status::kUnknownKind;
    r.log = "game_vocab: unknown/per-game kind " +
            std::to_string(cmd.kindValue) + " rejected";
    return r;
  }
  std::string log;
  Status s = ValidatePayload(cmd, log);
  if (s != Status::kOk) {
    ++g_violations;
    StageResult r;
    r.status = s;
    r.log = log;
    return r;
  }
  return StageResult{};
}

// CPU reference raster entry per kind: deterministic pixel derivation so the
// CPU path serves every accepted kind before any Skia leg reads staged bytes.
inline CpuPixel RasterizeReference(const GameCmd& cmd) {
  CpuPixel p;
  const std::uint8_t a =
      static_cast<std::uint8_t>(cmd.alpha * 255.0f + 0.5f);
  switch (cmd.kindValue) {
    case 1: p = {255, a, 0, a}; break;    // kSprite
    case 2: p = {0, 255, a, a}; break;    // kSpriteBatch
    case 3: p = {a, 0, 255, a}; break;    // kTileGrid
    case 4: p = {255, 255, a, a}; break;  // kPolyline
    case 5: p = {255, a, 255, a}; break;  // kEllipse
    case 6: p = {a, 255, 255, a}; break;  // kTextRun
    case 7: p = {128, a, 64, a}; break;   // kClipRect
    case 8: p = {64, 128, a, a}; break;   // kTransformMark
    case 9: p = {a, 64, 128, a}; break;   // kOpacityGroup
    case 11: p = {200, 200, 200, a}; break;  // kRect
    case 12: p = {180, 180, a, a}; break;    // kRoundedRect
    case 13: p = {0, 0, 0, a}; break;        // kShadow
    case 14: p = {32, 32, 32, a}; break;     // kScrim
    case 15: p = {a, a, a, 255}; break;      // kImage
    case 16: p = {255, 255, 255, a}; break;  // kIcon
    default: p = {0, 0, 0, 0}; break;        // Rejected kinds raster nothing.
  }
  return p;
}

inline void AppendLe32(FrameBytes& out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

inline void AppendLe64(FrameBytes& out, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
  }
}

inline std::uint32_t FloatBits(float f) {
  std::uint32_t u = 0;
  __builtin_memcpy(&u, &f, sizeof(u));
  return u;
}

inline CommitResult commit(StagedFrame f) {
  if (f.stackDepth < 0 || f.stackDepth > kMaxStackDepth) {
    ++g_violations;
    CommitResult r;
    r.ok = false;
    r.status = Status::kOverCap;
    r.log = "game_vocab: stack depth " + std::to_string(f.stackDepth) +
            " over cap 8 rejected";
    return r;
  }
  for (const GameCmd& cmd : f.commands) {
    if (!IsAcceptedKindValue(cmd.kindValue)) {
      ++g_violations;
      CommitResult r;
      r.ok = false;
      r.status = Status::kUnknownKind;
      r.log = "game_vocab: unknown/per-game kind " +
              std::to_string(cmd.kindValue) + " rejected";
      return r;
    }
    std::string log;
    if (ValidatePayload(cmd, log) != Status::kOk) {
      ++g_violations;
      CommitResult r;
      r.ok = false;
      r.status = Status::kOverCap;
      r.log = log;
      return r;
    }
  }
  // Whole-frame-or-nothing: bytes are emitted only after every command
  // validated, so a poisoned frame commits nothing (empty bytes).
  FrameBytes bytes;
  bytes.reserve(16 + f.commands.size() * 32);
  AppendLe64(bytes, f.frame);
  AppendLe32(bytes, static_cast<std::uint32_t>(f.commands.size()));
  AppendLe32(bytes, static_cast<std::uint32_t>(f.stackDepth));
  for (const GameCmd& cmd : f.commands) {
    AppendLe32(bytes, cmd.kindValue);
    AppendLe32(bytes, FloatBits(cmd.rotationRad));
    AppendLe32(bytes, FloatBits(cmd.u0));
    AppendLe32(bytes, FloatBits(cmd.v0));
    AppendLe32(bytes, FloatBits(cmd.u1));
    AppendLe32(bytes, FloatBits(cmd.v1));
    AppendLe32(bytes, FloatBits(cmd.alpha));
    AppendLe32(bytes, cmd.instances);
    const CpuPixel px = RasterizeReference(cmd);
    bytes.push_back(px.r);
    bytes.push_back(px.g);
    bytes.push_back(px.b);
    bytes.push_back(px.a);
  }
  ++g_committedFrames;
  CommitResult r;
  r.ok = true;
  r.bytes = bytes;
  r.status = Status::kOk;
  return r;
}

inline std::uint64_t CommittedFrameCount() { return g_committedFrames; }
inline std::uint64_t ViolationCount() { return g_violations; }

inline float EffectiveAlpha(float commandAlpha, float groupAlpha) {
  return commandAlpha * groupAlpha;
}

// ---- Test-support builders (coder-provided valid-payload surface) ----
inline GameCmd MakeValidCommand(GameKind kind) {
  GameCmd cmd;
  cmd.kindValue = static_cast<std::uint32_t>(kind);
  cmd.rotationRad = 0.0f;
  cmd.u0 = 0.0f;
  cmd.v0 = 0.0f;
  cmd.u1 = 1.0f;
  cmd.v1 = 1.0f;
  cmd.alpha = 1.0f;
  cmd.instances = 1;
  return cmd;
}

inline GameCmd MakeCommandWithKindValue(std::uint32_t v) {
  GameCmd cmd;
  cmd.kindValue = v;
  cmd.rotationRad = 0.0f;
  cmd.u0 = 0.0f;
  cmd.v0 = 0.0f;
  cmd.u1 = 1.0f;
  cmd.v1 = 1.0f;
  cmd.alpha = 1.0f;
  cmd.instances = 1;
  return cmd;
}

inline StagedFrame MakeValidFrame(std::uint64_t frameNumber) {
  StagedFrame f;
  f.frame = frameNumber;
  f.stackDepth = 2;
  f.commands.push_back(MakeValidCommand(GameKind::kSprite));
  f.commands.push_back(MakeValidCommand(GameKind::kTextRun));
  f.commands.push_back(MakeValidCommand(GameKind::kRect));
  return f;
}

inline StagedFrame MakeFrameWithInstances(std::uint32_t n) {
  StagedFrame f;
  f.frame = 1;
  f.stackDepth = 1;
  GameCmd cmd = MakeValidCommand(GameKind::kSpriteBatch);
  cmd.instances = n;
  f.commands.push_back(cmd);
  return f;
}

inline StagedFrame MakeFrameWithStackDepth(int depth) {
  StagedFrame f;
  f.frame = 1;
  f.stackDepth = depth;
  f.commands.push_back(MakeValidCommand(GameKind::kSprite));
  return f;
}

inline void AppendInvalidCommand(StagedFrame& f) {
  GameCmd cmd = MakeValidCommand(GameKind::kSpriteBatch);
  cmd.instances = kMaxBatchInstances + 1;  // Over-cap payload poisons frame.
  f.commands.push_back(cmd);
}

}  // namespace game_vocab

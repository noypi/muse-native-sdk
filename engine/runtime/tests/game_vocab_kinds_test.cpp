// Batch 1 RED pin — FR-GRAPHICS-018 / AC-GRAPHICS-048 (TC-GRAPHICS-051).
// Binds ONLY to the T1 `Interface:` line; never to implementation.
// Hermetic: C++17 + the `runtime/game_vocab.h` public header only; no
// device, no network, no env/config reads, no filesystem, no live services.
// Kind inventory + per-game-kind rejection per AC-048.
//
// Expected red: this TU does not compile until the coder publishes
// `engine/runtime/include/runtime/game_vocab.h` (+ sources + CMake
// registration) — `runtime/game_vocab.h: No such file or directory`.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/game_vocab.h"

namespace {

int failures = 0;

#define RUN_CASE(name)                                   \
  do {                                                   \
    if (!(name)()) {                                     \
      std::printf("FAIL %s\n", #name);                   \
      ++failures;                                        \
    } else {                                             \
      std::printf("PASS %s\n", #name);                   \
    }                                                    \
  } while (0)

// Every new AC-048 kind stages without kUnknownKind.
bool check_new_kinds_accepted() {
  using namespace game_vocab;
  const GameKind kinds[] = {
      GameKind::kSprite,       GameKind::kSpriteBatch, GameKind::kTileGrid,
      GameKind::kPolyline,     GameKind::kEllipse,     GameKind::kTextRun,
      GameKind::kClipRect,     GameKind::kTransformMark,
      GameKind::kOpacityGroup,
  };
  for (GameKind k : kinds) {
    GameCmd cmd = MakeValidCommand(k);  // coder-provided valid payload
    StageResult r = stage(cmd);
    if (r.status != Status::kOk) {
      std::printf("  kind %d rejected: %d\n", static_cast<int>(k),
                  static_cast<int>(r.status));
      return false;
    }
  }
  return true;
}

// Existing rect/rounded/shadow/scrim/image/icon kinds still stage.
bool check_existing_kinds_compose() {
  using namespace game_vocab;
  const GameKind kinds[] = {
      GameKind::kRect,     GameKind::kRoundedRect, GameKind::kShadow,
      GameKind::kScrim,    GameKind::kImage,       GameKind::kIcon,
  };
  for (GameKind k : kinds) {
    GameCmd cmd = MakeValidCommand(k);
    StageResult r = stage(cmd);
    if (r.status != Status::kOk) return false;
  }
  return true;
}

// Per-game kinds fail closed with kUnknownKind + log, never accepted.
bool check_per_game_kinds_rejected() {
  using namespace game_vocab;
  const GameKind kinds[] = {
      GameKind::kDrawFlappyScene,  // per-game name: must not exist as valid
      GameKind::kDrawFlappyBird,
      GameKind::kUnknown,
  };
  for (GameKind k : kinds) {
    GameCmd cmd = MakeValidCommand(k);
    StageResult r = stage(cmd);
    if (r.status != Status::kUnknownKind) {
      std::printf("  per-game kind %d not rejected: %d\n",
                  static_cast<int>(k), static_cast<int>(r.status));
      return false;
    }
    if (r.log.empty()) return false;  // violation must log
  }
  return true;
}

// Undeclared numeric kind values fail closed, never silently accepted.
bool check_undeclared_kind_rejected() {
  using namespace game_vocab;
  GameCmd cmd = MakeCommandWithKindValue(0x7FFF);  // outside declared range
  StageResult r = stage(cmd);
  return r.status == Status::kUnknownKind && !r.log.empty();
}

}  // namespace

int main() {
  RUN_CASE(check_new_kinds_accepted);
  RUN_CASE(check_existing_kinds_compose);
  RUN_CASE(check_per_game_kinds_rejected);
  RUN_CASE(check_undeclared_kind_rejected);
  if (failures > 0) {
    std::printf("game_vocab_kinds_test: %d failing\n", failures);
    return 1;
  }
  std::printf("game_vocab_kinds_test: all passing\n");
  return 0;
}

#pragma once

#include <cstdint>

#include "hit_test.h"
#include "input/event_model.h"

struct ArenaAction {
  int32_t winnerId = -1;
  float offsetX = 0.0f;
  float offsetY = 0.0f;
  bool claimed = false;
};

class GestureArena {
 public:
  ArenaAction AddPointer(const input::NormalizedEvent& ev, HitTestTree& tree);
  ArenaAction Claim(int32_t winnerId, int32_t pointerId);

 private:
  static constexpr int kMaxPointers = 32;
  struct PointerState {
    bool used = false;
    int32_t pointerId = 0;
    int32_t winnerId = -1;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    bool claimed = false;
  };
  PointerState pointers_[kMaxPointers]{};
  PointerState* find(int32_t pointerId);
  PointerState* allocate(int32_t pointerId);
};

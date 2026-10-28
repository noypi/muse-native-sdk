#include "gesture_arena.h"

GestureArena::PointerState* GestureArena::find(int32_t pointerId) {
  for (auto& p : pointers_) {
    if (p.used && p.pointerId == pointerId) return &p;
  }
  return nullptr;
}

GestureArena::PointerState* GestureArena::allocate(int32_t pointerId) {
  for (auto& p : pointers_) {
    if (!p.used) {
      p.used = true;
      p.pointerId = pointerId;
      p.winnerId = -1;
      p.offsetX = 0.0f;
      p.offsetY = 0.0f;
      p.claimed = false;
      return &p;
    }
  }
  return nullptr;
}

ArenaAction GestureArena::AddPointer(const input::NormalizedEvent& ev,
                                     HitTestTree& tree) {
  PointerState* state = find(ev.pointerId);
  if (state != nullptr && state->winnerId != -1) {
    // Winner lock: keep the current owner, accumulate the latest offset.
    state->offsetX = ev.axes.x;
    state->offsetY = ev.axes.y;
    return ArenaAction{state->winnerId, state->offsetX, state->offsetY,
                       state->claimed};
  }
  int32_t winner = tree.HitTest(ev.axes.x, ev.axes.y);
  if (state == nullptr) {
    state = allocate(ev.pointerId);
    if (state == nullptr) {
      return ArenaAction{winner, ev.axes.x, ev.axes.y, winner != -1};
    }
  }
  state->winnerId = winner;
  state->offsetX = ev.axes.x;
  state->offsetY = ev.axes.y;
  state->claimed = (winner != -1);
  return ArenaAction{state->winnerId, state->offsetX, state->offsetY,
                     state->claimed};
}

ArenaAction GestureArena::Claim(int32_t winnerId, int32_t pointerId) {
  PointerState* state = find(pointerId);
  if (state == nullptr) {
    return ArenaAction{-1, 0.0f, 0.0f, false};
  }
  // Escalate ownership, preserving the accumulated sub-pixel offset bit-exact.
  state->winnerId = winnerId;
  state->claimed = true;
  return ArenaAction{state->winnerId, state->offsetX, state->offsetY,
                     state->claimed};
}

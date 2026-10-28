#pragma once

#include <cstdint>

struct HitRect {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;

  bool contains(float px, float py) const;
};

class HitTestTree {
 public:
  void AddNode(int32_t id, int32_t parentId, HitRect bounds);
  int32_t HitTest(float x, float y) const;

 private:
  static constexpr int kMaxNodes = 64;
  struct Node {
    bool used = false;
    int32_t id = 0;
    int32_t parentId = 0;
    HitRect bounds{};
  };
  Node nodes_[kMaxNodes]{};
  int depth_of(int index) const;
  int find_index(int32_t id) const;
};

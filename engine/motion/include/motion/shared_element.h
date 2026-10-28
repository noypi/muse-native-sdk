#ifndef MOTION_SHARED_ELEMENT_H_
#define MOTION_SHARED_ELEMENT_H_

#include <string>
#include <vector>

#include "motion/layout_adapter.h"

namespace motion {

struct SharedAnchor {
  std::string elementId;
  std::string route;
  int depth = 0;
  Box box;
  float cornerRadius = 0.0f;
};

const SharedAnchor* motion_matchShared(const std::vector<SharedAnchor>& from,
                                       const std::vector<SharedAnchor>& to,
                                       const std::string& elementId) noexcept;

struct SharedFrame {
  Box box;
  float clipX = 0.0f;
  float clipY = 0.0f;
  float cornerRadius = 0.0f;
  float t01 = 0.0f;
};

SharedFrame motion_interpolateShared(const SharedAnchor& from,
                                     const SharedAnchor& to,
                                     float t01) noexcept;

}  // namespace motion

#endif  // MOTION_SHARED_ELEMENT_H_

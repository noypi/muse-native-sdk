#ifndef MOTION_M3_TOKENS_H_
#define MOTION_M3_TOKENS_H_

#include <map>
#include <string>

#include "motion/solver.h"

namespace motion {

// Token-name constants: components reference these names, never literals.
inline constexpr char kM3DurationShort1[] = "short1";
inline constexpr char kM3DurationShort2[] = "short2";
inline constexpr char kM3DurationShort3[] = "short3";
inline constexpr char kM3DurationShort4[] = "short4";
inline constexpr char kM3DurationMedium1[] = "medium1";
inline constexpr char kM3DurationMedium2[] = "medium2";
inline constexpr char kM3DurationMedium3[] = "medium3";
inline constexpr char kM3DurationMedium4[] = "medium4";
inline constexpr char kM3DurationLong1[] = "long1";
inline constexpr char kM3DurationLong2[] = "long2";
inline constexpr char kM3DurationLong3[] = "long3";
inline constexpr char kM3DurationLong4[] = "long4";

inline constexpr char kM3EasingStandard[] = "standard";
inline constexpr char kM3EasingEmphasized[] = "emphasized";
inline constexpr char kM3EasingAccelerate[] = "accelerate";
inline constexpr char kM3EasingDecelerate[] = "decelerate";

inline constexpr char kM3SpatialPosition[] = "position";
inline constexpr char kM3SpatialSize[] = "size";
inline constexpr char kM3SpatialShape[] = "shape";

inline constexpr char kM3EffectsColour[] = "colour";
inline constexpr char kM3EffectsOpacity[] = "opacity";

struct M3TokenSet {
  std::string version;
  std::map<std::string, float> durationsMs;
  std::map<std::string, std::string> easings;
  std::map<std::string, SpringSpec> spatialSprings;
  std::map<std::string, SpringSpec> effectsSprings;
};

M3TokenSet motion_loadM3Tokens(const std::string& jsonPath,
                               std::string* errorOut);

}  // namespace motion

#endif  // MOTION_M3_TOKENS_H_

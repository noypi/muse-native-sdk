#include "motion/spring_class.h"

#include "motion/m3_tokens.h"

namespace motion {
namespace {

const char* propertyName(AnimProperty prop) {
  switch (prop) {
    case AnimProperty::Position:
      return "Position";
    case AnimProperty::Size:
      return "Size";
    case AnimProperty::Shape:
      return "Shape";
    case AnimProperty::Colour:
      return "Colour";
    case AnimProperty::Opacity:
      return "Opacity";
  }
  return "Unknown";
}

bool isSpatialToken(const std::string& token) {
  return token == kM3SpatialPosition || token == kM3SpatialSize ||
         token == kM3SpatialShape;
}

bool isEffectsToken(const std::string& token) {
  return token == kM3EffectsColour || token == kM3EffectsOpacity;
}

void setError(std::string* errorOut, AnimProperty prop,
              const std::string& token, const char* reason) {
  if (errorOut == nullptr) return;
  *errorOut = std::string(reason) + " for property '" +
              propertyName(prop) + "' with token '" + token + "'";
}

}  // namespace

bool motion_checkSpringClass(AnimProperty prop, const std::string& token,
                             std::string* errorOut) {
  const bool wantsSpatial = (prop == AnimProperty::Position ||
                             prop == AnimProperty::Size ||
                             prop == AnimProperty::Shape);
  if (isSpatialToken(token)) {
    if (wantsSpatial) return true;
    setError(errorOut, prop, token,
             "spring class mismatch: spatial spring cannot drive");
    return false;
  }
  if (isEffectsToken(token)) {
    if (!wantsSpatial) return true;
    setError(errorOut, prop, token,
             "spring class mismatch: effects spring cannot drive");
    return false;
  }
  setError(errorOut, prop, token, "unknown spring token");
  return false;
}

}  // namespace motion

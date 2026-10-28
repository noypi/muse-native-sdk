#include "motion/tokens.h"

#include <cstring>

namespace motion {

const char* const kTokenSchemeVersion = "1";

TokenError::TokenError(std::string name, std::string reason)
    : Name(std::move(name)), Reason(std::move(reason)) {
  message_ = "motion token error: " + Name + ": " + Reason;
}

const char* TokenError::what() const noexcept {
  return message_.c_str();
}

namespace {

struct TableEntry {
  const char* name;
  MotionToken token;
};

// Version-pinned table (AC-042/AC-044). Durations increase along
// short1..long4; easings carry a named curve; springs carry positive
// physics constants. Values are pinned here and versioned above.
const TableEntry kTable[] = {
    {"duration-short1", {TokenKind::Duration, 50, nullptr, 0.0f, 0.0f}},
    {"duration-short2", {TokenKind::Duration, 100, nullptr, 0.0f, 0.0f}},
    {"duration-medium1", {TokenKind::Duration, 200, nullptr, 0.0f, 0.0f}},
    {"duration-medium2", {TokenKind::Duration, 300, nullptr, 0.0f, 0.0f}},
    {"duration-long1", {TokenKind::Duration, 400, nullptr, 0.0f, 0.0f}},
    {"duration-long2", {TokenKind::Duration, 500, nullptr, 0.0f, 0.0f}},
    {"duration-long3", {TokenKind::Duration, 700, nullptr, 0.0f, 0.0f}},
    {"duration-long4", {TokenKind::Duration, 900, nullptr, 0.0f, 0.0f}},
    {"easing-standard", {TokenKind::Easing, 0, "cubic-bezier(0.2,0,0,1)", 0.0f, 0.0f}},
    {"easing-emphasized", {TokenKind::Easing, 0, "cubic-bezier(0.05,0.7,0.1,1)", 0.0f, 0.0f}},
    {"easing-accelerate", {TokenKind::Easing, 0, "cubic-bezier(0.3,0,0.8,0.15)", 0.0f, 0.0f}},
    {"easing-decelerate", {TokenKind::Easing, 0, "cubic-bezier(0.05,0.7,0.1,1)", 0.0f, 0.0f}},
    {"spatial-default", {TokenKind::SpatialSpring, 0, nullptr, 200.0f, 25.0f}},
    {"effects-default", {TokenKind::EffectsSpring, 0, nullptr, 150.0f, 20.0f}},
};

bool isGeometry(const char* property) {
  return std::strcmp(property, "position") == 0 ||
         std::strcmp(property, "size") == 0 ||
         std::strcmp(property, "shape") == 0;
}

bool isEffects(const char* property) {
  return std::strcmp(property, "opacity") == 0 ||
         std::strcmp(property, "color") == 0 ||
         std::strcmp(property, "colour") == 0;
}

}  // namespace

MotionToken resolveToken(const char* name) {
  if (name == nullptr) {
    throw TokenError("null", "token name must not be null");
  }
  if (name[0] == '\0') {
    throw TokenError("empty", "unknown token: empty token name");
  }
  for (const TableEntry& entry : kTable) {
    if (std::strcmp(entry.name, name) == 0) {
      return entry.token;
    }
  }
  throw TokenError(name, std::string("unknown token: ") + name);
}

void checkSpringClass(SpringClass c, const char* property) {
  if (property == nullptr || property[0] == '\0') {
    throw TokenError(property == nullptr ? "null" : "empty",
                     "unknown property for spring class check");
  }
  if (c == SpringClass::Spatial) {
    if (isEffects(property)) {
      throw TokenError(property,
                       std::string("spatial spring must not drive effects property '") +
                           property + "'");
    }
    if (!isGeometry(property)) {
      throw TokenError(property,
                       std::string("unknown property for spatial spring: '") +
                           property + "'");
    }
    return;
  }
  if (isGeometry(property)) {
    throw TokenError(property, std::string("effects spring must not drive geometry property '") +
                                   property + "'");
  }
  if (!isEffects(property)) {
    throw TokenError(property, std::string("unknown property for effects spring: '") +
                                   property + "'");
  }
}

}  // namespace motion

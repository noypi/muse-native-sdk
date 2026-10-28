#pragma once

#include <exception>
#include <string>

namespace motion {

enum class TokenKind {
  Duration,
  Easing,
  SpatialSpring,
  EffectsSpring,
};

enum class SpringClass {
  Spatial,
  Effects,
};

struct MotionToken {
  TokenKind kind = TokenKind::Duration;
  int durationMs = 0;
  const char* easing = nullptr;
  float stiffness = 0.0f;
  float damping = 0.0f;
};

// Version pin for the token scheme (AC-044); any value change ships with a
// bumped version rather than a silent drift.
extern const char* const kTokenSchemeVersion;

// Thrown by resolveToken/checkSpringClass; what() always names the token
// (and property where applicable) without carrying secrets.
class TokenError : public std::exception {
 public:
  TokenError(std::string name, std::string reason);
  const char* what() const noexcept override;
  std::string Name;
  std::string Reason;

 private:
  std::string message_;
};

// Looks up a duration/easing/spring token by name; throws TokenError naming
// the token when the name is null, empty, or unknown.
MotionToken resolveToken(const char* name);

// Rejects class misuse: an effects spring driving geometry (position, size,
// shape) or a spatial spring driving effects (color/colour, opacity) throws
// TokenError naming the property and the token class. Correct pairings pass.
void checkSpringClass(SpringClass c, const char* property);

}  // namespace motion

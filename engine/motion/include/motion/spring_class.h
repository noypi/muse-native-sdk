#ifndef MOTION_SPRING_CLASS_H_
#define MOTION_SPRING_CLASS_H_

#include <string>

namespace motion {

enum class AnimProperty { Position, Size, Shape, Colour, Opacity };

enum class SpringClass { Spatial, Effects };

bool motion_checkSpringClass(AnimProperty prop, const std::string& token,
                             std::string* errorOut);

// Compile-time guard: the spring class is carried as a constant so a static
// path that selects the wrong class for a property fails to compile at the
// use site via CheckProperty<P>().
template <SpringClass C>
struct TypedSpring {
  static constexpr SpringClass kClass = C;

  template <AnimProperty P>
  static constexpr void CheckProperty() {
    static_assert(
        (C == SpringClass::Spatial &&
         (P == AnimProperty::Position || P == AnimProperty::Size ||
          P == AnimProperty::Shape)) ||
            (C == SpringClass::Effects &&
             (P == AnimProperty::Colour || P == AnimProperty::Opacity)),
        "spring class mismatch for property");
  }
};

}  // namespace motion

#endif  // MOTION_SPRING_CLASS_H_

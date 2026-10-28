#ifndef GRAPHICS_CAPS_VALIDATION_GATE_H_
#define GRAPHICS_CAPS_VALIDATION_GATE_H_

// Single BuildType definition lives in shaders/shader_bundle.h (T7); reuse
// it here so both headers can be included without an ODR clash.
#include "shaders/shader_bundle.h"

namespace graphics {

// Single choke point gating Khronos validation layer loading: true for
// DEV|PROFILE, false for RELEASE and any unknown value (fail closed).
bool validation_enabled(BuildType build);

}  // namespace graphics

#endif  // GRAPHICS_CAPS_VALIDATION_GATE_H_

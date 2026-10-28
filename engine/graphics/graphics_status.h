// Global graphics status (FR-GRAPHICS-007/008/009).
// Single owner of the global `::Status` superset used by the texture pool
// (T4), the scene module gate (T5), and the scene composite (T6). Global
// scope only; no other declarations.
#ifndef ENGINE_GRAPHICS_GRAPHICS_STATUS_H_
#define ENGINE_GRAPHICS_GRAPHICS_STATUS_H_

enum class Status { kOk, kNotLinked, kOverBudget, kInvalidArg };

#endif  // ENGINE_GRAPHICS_GRAPHICS_STATUS_H_

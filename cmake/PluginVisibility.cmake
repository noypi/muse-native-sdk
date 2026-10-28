# Plugin visibility + render-path exception flags (NFR-CORE-006, AC-CORE-008/010).
#
# Plugin symbols default to hidden visibility; only explicitly exported
# entry points are visible. The render-path TU additionally compiles
# with -fno-exceptions so no exception can cross the render boundary
# (escapes route to renderPathViolation and abort).

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# Scope -fno-exceptions to the render-path TU only; non-render code
# may still use exceptions.
set_source_files_properties(
  "${CMAKE_CURRENT_SOURCE_DIR}/engine/core/render_path_tu.cpp"
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions"
)

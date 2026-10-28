cmake_minimum_required(VERSION 3.16)
project(muse_safety CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# Sanitizer + hardening matrix (T1). Per-target flags only; this module
# never mutates global CMAKE_CXX_FLAGS.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/Sanitizers.cmake)

# Production sources (T2): secure zeroization helper, linked the same
# way engine/core/bridge tests link `bridge`.
add_library(safety
  src/zeroize.cc
)
target_include_directories(safety PUBLIC include)
target_compile_options(safety PRIVATE -Wall -Wextra -Wpedantic)
muse_apply_sanitizers(safety)

include(CTest)
enable_testing()

# One test executable per tests/*_test.cpp. CTest suite names are the
# T3 interface (`safety_uaf`, `safety_oob`, `safety_zeroize`); each
# suite exits 0 on a clean (non-sanitizer) configure. The seeded
# defects in the uaf/oob suites are argv-gated `--seed` negative
# tests: they never run under ctest, and abort with the documented
# ASan signature only under `-DMUSE_SANITIZER=asan` (see T5).
add_executable(safety_uaf tests/sanitizer_uaf_test.cpp)
target_compile_options(safety_uaf PRIVATE -Wall -Wextra -Wpedantic)
muse_apply_sanitizers(safety_uaf)
add_test(NAME safety_uaf COMMAND safety_uaf)

add_executable(safety_oob tests/sanitizer_oob_test.cpp)
target_compile_options(safety_oob PRIVATE -Wall -Wextra -Wpedantic)
muse_apply_sanitizers(safety_oob)
add_test(NAME safety_oob COMMAND safety_oob)

add_executable(safety_zeroize tests/zeroize_test.cpp)
target_link_libraries(safety_zeroize PRIVATE safety)
target_compile_options(safety_zeroize PRIVATE -Wall -Wextra -Wpedantic)
muse_apply_sanitizers(safety_zeroize)
add_test(NAME safety_zeroize COMMAND safety_zeroize)

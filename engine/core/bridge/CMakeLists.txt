cmake_minimum_required(VERSION 3.16)
project(muse_bridge CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Production sources are coder-owned (plan T1/T2 Files: src/envelope.cc).
# The library target exists so the batch test executables link the same
# way engine/net tests link `net`.
add_library(bridge
  src/envelope.cc
)
target_include_directories(bridge PUBLIC include)
target_compile_options(bridge PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
enable_testing()

# One test executable per tests/*_test.cpp, linked against `bridge`, so
# the plan's `cmake -S engine/core/bridge -B <dir> && cmake --build
# <dir> && ctest --test-dir <dir>` path runs the batch suites.
# Batch 1 wires bridge_envelope + bridge_mismatch; Batch 2 (T3, T4)
# appends ring_buffer + bridge_fuzz here — the tester must not add them
# early (GR-08 T1.7).
foreach(TEST_SRC
    tests/bridge_envelope_test.cpp
    tests/bridge_mismatch_test.cpp
    tests/ring_buffer_test.cpp
    tests/bridge_fuzz_test.cpp)
  get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
  add_executable(${TEST_NAME} ${TEST_SRC})
  target_link_libraries(${TEST_NAME} PRIVATE bridge)
  target_compile_options(${TEST_NAME} PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()

cmake_minimum_required(VERSION 3.16)
project(muse_sandbox CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Production sources are coder-owned (plan T5 Files: src/verifier.cc).
add_library(sandbox
  src/verifier.cc
)
target_include_directories(sandbox PUBLIC include)
target_compile_options(sandbox PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
enable_testing()

# One test executable per tests/*_test.cpp, linked against `sandbox`, so
# the plan's `cmake -S engine/core/sandbox -B <dir> && cmake --build
# <dir> && ctest --test-dir <dir>` path runs the batch suite.
foreach(TEST_SRC
    tests/sandbox_test.cpp)
  get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
  add_executable(${TEST_NAME} ${TEST_SRC})
  target_link_libraries(${TEST_NAME} PRIVATE sandbox)
  target_compile_options(${TEST_NAME} PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()

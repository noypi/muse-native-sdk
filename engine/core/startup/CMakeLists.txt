cmake_minimum_required(VERSION 3.16)
project(muse_startup CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Production sources are coder-owned (plan T1 Files: src/bootstrap.cc;
# plan T2 Files: src/dynamic_init.cc; plan T3 Files: src/pool_warmup.cc).
add_library(startup
  src/bootstrap.cc
  src/dynamic_init.cc
  src/pool_warmup.cc
  src/trace.cc
)
target_include_directories(startup PUBLIC include)
target_compile_options(startup PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
enable_testing()

# One test executable per tests/*_test.cpp, linked against `startup`, so
# the plan's `cmake -S engine/core/startup -B <dir> && cmake --build
# <dir> && ctest --test-dir <dir>` path runs the batch suites.
# Batch 1 wires bootstrap_test here; T2 appends dynamic_init_test plus
# the `dynamicInitCheck` target — the tester must not add them early.
foreach(TEST_SRC
    tests/bootstrap_test.cpp
    tests/dynamic_init_test.cpp
    tests/pool_warmup_test.cpp
    tests/startup_gate_test.cpp)
  get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
  add_executable(${TEST_NAME} ${TEST_SRC})
  target_link_libraries(${TEST_NAME} PRIVATE startup)
  target_compile_options(${TEST_NAME} PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()

# Hermetic sentinel for dynamic_init_test's clean-tree case: the test calls
# check_dynamic_initializers("test-fixtures/startup-clean-empty") expecting
# 0, resolved relative to the test working directory
# (CMAKE_CURRENT_BINARY_DIR). An empty dir scans zero objects and passes;
# a missing dir fails closed, so the directory must exist at test time.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/startup-clean-empty)

# AC-CORE-031 CI gate (TC-CORE-031): scan the built startup-path
# production objects with tools/check_dynamic_init.py (llvm-nm/readelf-based,
# same approach as plugin/tests/visibility_check.sh). Scoped to the
# `startup` library objects only: test executables legitimately pull in
# standard-library dynamic initializers and are not the startup path.
# A nonzero script exit fails the build. Backs
# `./gradlew :engine:core:dynamicInitCheck` (plan A2).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_custom_target(dynamicInitCheck
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/../../../tools/check_dynamic_init.py
      ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/startup.dir
    COMMENT "AC-CORE-031: no dynamic initializers on the startup path"
    VERBATIM)
  add_dependencies(dynamicInitCheck startup bootstrap_test dynamic_init_test)
endif()

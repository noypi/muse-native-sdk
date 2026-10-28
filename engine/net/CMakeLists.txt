cmake_minimum_required(VERSION 3.16)
project(muse_net CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(net
  src/transport.cc
  src/http_cache.cc
  src/retry.cc
  src/resilience.cc
)
target_include_directories(net PUBLIC include)
target_compile_options(net PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
enable_testing()

# One test executable per tests/*_test.cpp, linked against `net`, so the
# plan's `cmake -S engine/net -B <dir> && cmake --build <dir> &&
# ctest --test-dir <dir>` path runs all six suites.
foreach(TEST_SRC
    tests/transport_api_test.cpp
    tests/http_dispatch_test.cpp
    tests/http_cache_rfc7234_test.cpp
    tests/cache_ownership_test.cpp
    tests/retry_idempotency_test.cpp
    tests/resilience_test.cpp)
  get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
  add_executable(${TEST_NAME} ${TEST_SRC})
  target_link_libraries(${TEST_NAME} PRIVATE net)
  target_compile_options(${TEST_NAME} PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()

cmake_minimum_required(VERSION 3.16)
project(muse_core_mem CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(core_mem
  src/arena.cpp
)
target_include_directories(core_mem PUBLIC include)
target_compile_options(core_mem PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
enable_testing()

add_executable(arena_reset_test tests/arena_reset_test.cpp)
target_link_libraries(arena_reset_test PRIVATE core_mem)
target_compile_options(arena_reset_test PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME arena_reset_test COMMAND arena_reset_test)

add_executable(allocator_guard_test tests/allocator_guard_test.cpp)
target_link_libraries(allocator_guard_test PRIVATE core_mem)
target_compile_options(allocator_guard_test PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME allocator_guard_test COMMAND allocator_guard_test)

target_sources(core_mem PRIVATE src/leak_hooks.cpp)

add_executable(leak_hooks_test tests/leak_hooks_test.cpp)
target_link_libraries(leak_hooks_test PRIVATE core_mem)
target_compile_options(leak_hooks_test PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME leak_hooks_test COMMAND leak_hooks_test)

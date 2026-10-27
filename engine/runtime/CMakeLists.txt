cmake_minimum_required(VERSION 3.16)
project(muse_runtime CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(runtime
  src/tree.cpp
  src/frame.cpp
  src/layout.cpp
)
target_include_directories(runtime PUBLIC include)
target_compile_options(runtime PRIVATE -Wall -Wextra -Wpedantic)

add_executable(instantiate_test tests/instantiate_test.cpp)
target_link_libraries(instantiate_test PRIVATE runtime)
target_compile_options(instantiate_test PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
enable_testing()
add_test(NAME instantiate_test COMMAND instantiate_test)

add_executable(invalidation_test tests/invalidation_test.cpp)
target_link_libraries(invalidation_test PRIVATE runtime)
target_compile_options(invalidation_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME invalidation_test COMMAND invalidation_test)

add_executable(idle_frames_test tests/idle_frames_test.cpp)
target_link_libraries(idle_frames_test PRIVATE runtime)
target_compile_options(idle_frames_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME idle_frames_test COMMAND idle_frames_test)

add_executable(layout_test tests/layout_test.cpp)
target_link_libraries(layout_test PRIVATE runtime)
target_compile_options(layout_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME layout_test COMMAND layout_test)

add_executable(relayout_scope_test tests/relayout_scope_test.cpp)
target_link_libraries(relayout_scope_test PRIVATE runtime)
target_compile_options(relayout_scope_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME relayout_scope_test COMMAND relayout_scope_test)

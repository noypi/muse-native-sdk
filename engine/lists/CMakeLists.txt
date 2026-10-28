cmake_minimum_required(VERSION 3.16)
project(muse_lists CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(lists STATIC src/stable_key.cpp src/myers_diff.cpp)
target_include_directories(lists PUBLIC include)
target_compile_options(lists PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(stable_key_test tests/stable_key_test.cpp)
target_link_libraries(stable_key_test PRIVATE lists)
target_compile_options(stable_key_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(myers_diff_test tests/myers_diff_test.cpp)
target_link_libraries(myers_diff_test PRIVATE lists)
target_compile_options(myers_diff_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

include(CTest)
enable_testing()
add_test(NAME stable_key_test COMMAND stable_key_test)
add_test(NAME myers_diff_test COMMAND myers_diff_test)

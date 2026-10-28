cmake_minimum_required(VERSION 3.16)
project(muse_state CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(state src/atom.cpp src/store.cpp)
target_include_directories(state PUBLIC include)
target_compile_options(state PRIVATE -Wall -Wextra -Wpedantic)

add_executable(atom_core_test tests/atom_core_test.cpp)
target_link_libraries(atom_core_test PRIVATE state)
target_compile_options(atom_core_test PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
enable_testing()
add_test(NAME atom_core_test COMMAND atom_core_test)

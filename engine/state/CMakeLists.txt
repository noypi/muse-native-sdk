cmake_minimum_required(VERSION 3.16)
project(muse_state CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(state src/atom.cpp src/store.cpp src/provenance.cpp src/frame.cpp)
target_include_directories(state PUBLIC include)
target_compile_options(state PRIVATE -Wall -Wextra -Wpedantic)

add_executable(atom_core_test tests/atom_core_test.cpp)
target_link_libraries(atom_core_test PRIVATE state)
target_compile_options(atom_core_test PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
enable_testing()
add_test(NAME atom_core_test COMMAND atom_core_test)

add_executable(tier2_provenance_test tests/tier2_provenance_test.cpp)
target_link_libraries(tier2_provenance_test PRIVATE state)
target_compile_options(tier2_provenance_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME tier2_provenance_test COMMAND tier2_provenance_test)

add_executable(native_path_test tests/native_path_test.cpp)
target_link_libraries(native_path_test PRIVATE state)
target_compile_options(native_path_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME native_path_test COMMAND native_path_test)

add_executable(tier2_frame_test tests/tier2_frame_test.cpp)
target_link_libraries(tier2_frame_test PRIVATE state)
target_compile_options(tier2_frame_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME tier2_frame_test COMMAND tier2_frame_test)

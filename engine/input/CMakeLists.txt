cmake_minimum_required(VERSION 3.16)
project(muse_input CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(input STATIC src/event_model.cpp src/hit_test.cpp src/gesture_arena.cpp src/back_gesture.cpp)
target_include_directories(input PUBLIC include)
target_compile_options(input PRIVATE -Wall -Wextra -Wpedantic)

add_executable(event_model_test tests/event_model_test.cpp)
target_include_directories(event_model_test PRIVATE include)
target_link_libraries(event_model_test PRIVATE input)
target_compile_options(event_model_test PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
enable_testing()
add_test(NAME event_model_test COMMAND event_model_test)

add_executable(gesture_arena_test tests/gesture_arena_test.cpp)
target_include_directories(gesture_arena_test PRIVATE include)
target_link_libraries(gesture_arena_test PRIVATE input)
target_compile_options(gesture_arena_test PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME gesture_arena_test COMMAND gesture_arena_test)

add_executable(back_progress_test tests/back_progress_test.cpp)
target_include_directories(back_progress_test PRIVATE include)
target_link_libraries(back_progress_test PRIVATE input)
target_compile_options(back_progress_test PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME back_progress_test COMMAND back_progress_test)

add_executable(inset_ownership_test tests/inset_ownership_test.cpp)
target_include_directories(inset_ownership_test PRIVATE include)
target_link_libraries(inset_ownership_test PRIVATE input)
target_compile_options(inset_ownership_test PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME inset_ownership_test COMMAND inset_ownership_test)

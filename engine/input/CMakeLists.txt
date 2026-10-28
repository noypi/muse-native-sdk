cmake_minimum_required(VERSION 3.16)
project(muse_input CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(input STATIC src/event_model.cpp src/hit_test.cpp src/gesture_arena.cpp src/back_gesture.cpp src/focus_tree.cpp src/focus_restore.cpp src/device_harness.cpp src/stylus_history.cpp src/haptics_settings.cpp)
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

add_executable(focus_traversal_test tests/focus_traversal_test.cpp)
target_include_directories(focus_traversal_test PRIVATE include)
target_link_libraries(focus_traversal_test PRIVATE input)
target_compile_options(focus_traversal_test PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME focus_traversal_test COMMAND focus_traversal_test)

add_executable(focus_group_test tests/focus_group_test.cpp)
target_include_directories(focus_group_test PRIVATE include)
target_link_libraries(focus_group_test PRIVATE input)
target_compile_options(focus_group_test PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME focus_group_test COMMAND focus_group_test)

add_executable(device_coverage_test tests/device_coverage_test.cpp)
target_include_directories(device_coverage_test PRIVATE include)
target_link_libraries(device_coverage_test PRIVATE input)
target_compile_options(device_coverage_test PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME device_coverage_test COMMAND device_coverage_test)

add_executable(focus_restore_test tests/focus_restore_test.cpp)
target_include_directories(focus_restore_test PRIVATE include)
target_link_libraries(focus_restore_test PRIVATE input)
target_compile_options(focus_restore_test PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME focus_restore_test COMMAND focus_restore_test)

add_executable(focus_rebuild_test tests/focus_rebuild_test.cpp)
target_include_directories(focus_rebuild_test PRIVATE include)
target_link_libraries(focus_rebuild_test PRIVATE input)
target_compile_options(focus_rebuild_test PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME focus_rebuild_test COMMAND focus_rebuild_test)

add_executable(stylus_history_test tests/stylus_history_test.cpp)
target_include_directories(stylus_history_test PRIVATE include)
target_link_libraries(stylus_history_test PRIVATE input)
target_compile_options(stylus_history_test PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME stylus_history_test COMMAND stylus_history_test)

add_executable(haptics_settings_test tests/haptics_settings_test.cpp)
target_include_directories(haptics_settings_test PRIVATE include)
target_link_libraries(haptics_settings_test PRIVATE input)
target_compile_options(haptics_settings_test PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME haptics_settings_test COMMAND haptics_settings_test)

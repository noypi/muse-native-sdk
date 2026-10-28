cmake_minimum_required(VERSION 3.16)
project(muse_lists CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(lists STATIC src/stable_key.cpp src/myers_diff.cpp src/scroll_physics.cpp src/scroll_signal.cpp src/refresh_rate.cpp src/position_persist.cpp src/snap_refresh.cpp src/item_animator.cpp src/drag_reorder.cpp src/swipe_dismiss.cpp src/selection_mode.cpp src/collection_contract.cpp src/letter_jump.cpp src/sticky_header.cpp src/slot_state.cpp src/collection_shape.cpp)
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

add_executable(physics_integrate_test tests/physics_integrate_test.cpp)
target_link_libraries(physics_integrate_test PRIVATE lists)
target_compile_options(physics_integrate_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME physics_integrate_test COMMAND physics_integrate_test)

add_executable(scroll_signal_test tests/scroll_signal_test.cpp)
target_link_libraries(scroll_signal_test PRIVATE lists)
target_compile_options(scroll_signal_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME scroll_signal_test COMMAND scroll_signal_test)

add_executable(refresh_rate_test tests/refresh_rate_test.cpp)
target_link_libraries(refresh_rate_test PRIVATE lists)
target_include_directories(refresh_rate_test PRIVATE include)
target_compile_options(refresh_rate_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME refresh_rate_test COMMAND refresh_rate_test)

add_executable(position_persist_test tests/position_persist_test.cpp)
target_link_libraries(position_persist_test PRIVATE lists)
target_include_directories(position_persist_test PRIVATE include)
target_compile_options(position_persist_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME position_persist_test COMMAND position_persist_test)

add_executable(diff_benchmark_test tests/diff_benchmark_test.cpp)
target_link_libraries(diff_benchmark_test PRIVATE lists)
target_include_directories(diff_benchmark_test PRIVATE include)
target_compile_options(diff_benchmark_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME diff_benchmark_test COMMAND diff_benchmark_test)

add_executable(item_animator_test tests/item_animator_test.cpp)
target_link_libraries(item_animator_test PRIVATE lists)
target_include_directories(item_animator_test PRIVATE include)
target_compile_options(item_animator_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME item_animator_test COMMAND item_animator_test)

add_executable(drag_reorder_test tests/drag_reorder_test.cpp)
target_link_libraries(drag_reorder_test PRIVATE lists)
target_include_directories(drag_reorder_test PRIVATE include)
target_compile_options(drag_reorder_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME drag_reorder_test COMMAND drag_reorder_test)

add_executable(swipe_dismiss_test tests/swipe_dismiss_test.cpp)
target_link_libraries(swipe_dismiss_test PRIVATE lists)
target_include_directories(swipe_dismiss_test PRIVATE include)
target_compile_options(swipe_dismiss_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME swipe_dismiss_test COMMAND swipe_dismiss_test)

add_executable(selection_mode_test tests/selection_mode_test.cpp)
target_link_libraries(selection_mode_test PRIVATE lists)
target_include_directories(selection_mode_test PRIVATE include)
target_compile_options(selection_mode_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME selection_mode_test COMMAND selection_mode_test)

add_executable(collection_contract_test tests/collection_contract_test.cpp)
target_link_libraries(collection_contract_test PRIVATE lists)
target_include_directories(collection_contract_test PRIVATE include)
target_compile_options(collection_contract_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME collection_contract_test COMMAND collection_contract_test)

add_executable(letter_jump_test tests/letter_jump_test.cpp)
target_link_libraries(letter_jump_test PRIVATE lists)
target_include_directories(letter_jump_test PRIVATE include)
target_compile_options(letter_jump_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME letter_jump_test COMMAND letter_jump_test)

add_executable(sticky_header_test tests/sticky_header_test.cpp)
target_link_libraries(sticky_header_test PRIVATE lists)
target_include_directories(sticky_header_test PRIVATE include)
target_compile_options(sticky_header_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME sticky_header_test COMMAND sticky_header_test)

add_executable(slot_state_test tests/slot_state_test.cpp)
target_link_libraries(slot_state_test PRIVATE lists)
target_include_directories(slot_state_test PRIVATE include)
target_compile_options(slot_state_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME slot_state_test COMMAND slot_state_test)

add_executable(collection_shape_test tests/collection_shape_test.cpp)
target_link_libraries(collection_shape_test PRIVATE lists)
target_include_directories(collection_shape_test PRIVATE include)
target_compile_options(collection_shape_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME collection_shape_test COMMAND collection_shape_test)

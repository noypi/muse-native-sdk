cmake_minimum_required(VERSION 3.16)
project(muse_lists CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(lists STATIC src/stable_key.cpp src/myers_diff.cpp src/scroll_physics.cpp src/scroll_signal.cpp)
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

add_executable(refresh_rate_test tests/refresh_rate_test.cpp src/refresh_rate.cpp)
target_include_directories(refresh_rate_test PRIVATE include)
target_compile_options(refresh_rate_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME refresh_rate_test COMMAND refresh_rate_test)

add_executable(position_persist_test tests/position_persist_test.cpp src/position_persist.cpp)
target_link_libraries(position_persist_test PRIVATE lists)
target_include_directories(position_persist_test PRIVATE include)
target_compile_options(position_persist_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME position_persist_test COMMAND position_persist_test)

add_executable(diff_benchmark_test tests/diff_benchmark_test.cpp src/snap_refresh.cpp)
target_link_libraries(diff_benchmark_test PRIVATE lists)
target_include_directories(diff_benchmark_test PRIVATE include)
target_compile_options(diff_benchmark_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME diff_benchmark_test COMMAND diff_benchmark_test)

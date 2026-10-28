cmake_minimum_required(VERSION 3.16)
project(muse_runtime CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(runtime
  src/tree.cpp
  src/frame.cpp
  src/layout.cpp
  src/js_runtime.cpp
  src/js_engine.cpp
  src/vulkan_baseline.cpp
  src/renderer.cpp
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

add_executable(js_abi_isolation_test tests/js_abi_isolation_test.cpp)
target_link_libraries(js_abi_isolation_test PRIVATE runtime)
target_compile_options(js_abi_isolation_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME js_abi_isolation_test COMMAND js_abi_isolation_test)

add_executable(js_execution_test tests/js_execution_test.cpp)
target_link_libraries(js_execution_test PRIVATE runtime)
target_compile_options(js_execution_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME js_execution_test COMMAND js_execution_test)

add_executable(vulkan_baseline_test tests/vulkan_baseline_test.cpp)
target_link_libraries(vulkan_baseline_test PRIVATE runtime)
target_compile_options(vulkan_baseline_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME vulkan_baseline_test COMMAND vulkan_baseline_test)

add_executable(request_frame_test tests/request_frame_test.cpp)
target_link_libraries(request_frame_test PRIVATE runtime)
target_compile_options(request_frame_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME request_frame_test COMMAND request_frame_test)

add_executable(continuous_mode_test tests/continuous_mode_test.cpp)
target_link_libraries(continuous_mode_test PRIVATE runtime)
target_compile_options(continuous_mode_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME continuous_mode_test COMMAND continuous_mode_test)

add_executable(js_lifecycle_test tests/js_lifecycle_test.cpp)
target_link_libraries(js_lifecycle_test PRIVATE runtime)
target_compile_options(js_lifecycle_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME js_lifecycle_test COMMAND js_lifecycle_test)

add_executable(frame_budget_telemetry_test tests/frame_budget_telemetry_test.cpp)
target_link_libraries(frame_budget_telemetry_test PRIVATE runtime)
target_compile_options(frame_budget_telemetry_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME frame_budget_telemetry_test COMMAND frame_budget_telemetry_test)

add_executable(render_smoke_test tests/render_smoke_test.cpp)
target_link_libraries(render_smoke_test PRIVATE runtime)
target_compile_options(render_smoke_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME render_smoke_test COMMAND render_smoke_test)

add_executable(text_metrics_test tests/text_metrics_test.cpp)
target_link_libraries(text_metrics_test PRIVATE runtime)
target_compile_options(text_metrics_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME text_metrics_test COMMAND text_metrics_test)

add_executable(text_editing_test tests/text_editing_test.cpp)
target_link_libraries(text_editing_test PRIVATE runtime)
target_compile_options(text_editing_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME text_editing_test COMMAND text_editing_test)

add_executable(ime_inset_test tests/ime_inset_test.cpp)
target_link_libraries(ime_inset_test PRIVATE runtime)
target_compile_options(ime_inset_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME ime_inset_test COMMAND ime_inset_test)

add_executable(host_lifecycle_test tests/host_lifecycle_test.cpp)
target_link_libraries(host_lifecycle_test PRIVATE runtime)
target_compile_options(host_lifecycle_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME host_lifecycle_test COMMAND host_lifecycle_test)

add_executable(touch_dispatch_test tests/touch_dispatch_test.cpp)
target_link_libraries(touch_dispatch_test PRIVATE runtime)
target_compile_options(touch_dispatch_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME touch_dispatch_test COMMAND touch_dispatch_test)

add_executable(config_change_test tests/config_change_test.cpp)
target_link_libraries(config_change_test PRIVATE runtime)
target_compile_options(config_change_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME config_change_test COMMAND config_change_test)

add_executable(gap_buffer_test tests/gap_buffer_test.cpp)
target_link_libraries(gap_buffer_test PRIVATE runtime)
target_compile_options(gap_buffer_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME gap_buffer_test COMMAND gap_buffer_test)

add_executable(alooper_bridge_test tests/alooper_bridge_test.cpp)
target_link_libraries(alooper_bridge_test PRIVATE runtime)
target_compile_options(alooper_bridge_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME alooper_bridge_test COMMAND alooper_bridge_test)

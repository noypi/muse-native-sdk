cmake_minimum_required(VERSION 3.16)
project(muse_motion CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(motion
  src/solver.cpp
  src/spring.cpp
  src/graph.cpp
  src/gesture_binding.cpp
  src/arbitration.cpp
  src/layout_adapter.cpp
  src/implicit.cpp
  src/route_transition.cpp
  src/clock.cpp
  src/recorder.cpp
  src/trace.cpp
  src/determinism.cpp
  src/asset.cpp
  src/vector_importer.cpp
  src/asset_prepare.cpp
  src/asset_scale.cpp
  src/svg_import.cpp
  src/shared_element.cpp
  src/large_motion.cpp
  src/m3_tokens.cpp
  src/spring_class.cpp
  src/tokens.cpp
  src/patterns.cpp
  src/reduced_motion.cpp
)
target_include_directories(motion PUBLIC include)
target_compile_options(motion PRIVATE -Wall -Wextra -Wpedantic)
find_package(Threads REQUIRED)
target_link_libraries(motion PUBLIC Threads::Threads)

add_executable(spring_solver_test tests/spring_solver_test.cpp)
target_link_libraries(spring_solver_test PRIVATE motion)
target_compile_options(spring_solver_test PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
enable_testing()
add_test(NAME spring_solver_test COMMAND spring_solver_test)

add_executable(graph_compose_test tests/graph_compose_test.cpp)
target_link_libraries(graph_compose_test PRIVATE motion)
target_compile_options(graph_compose_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME graph_compose_test COMMAND graph_compose_test)

add_executable(gesture_binding_test tests/gesture_binding_test.cpp)
target_link_libraries(gesture_binding_test PRIVATE motion)
target_compile_options(gesture_binding_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME gesture_binding_test COMMAND gesture_binding_test)

add_executable(arbitration_test tests/arbitration_test.cpp)
target_link_libraries(arbitration_test PRIVATE motion)
target_compile_options(arbitration_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME arbitration_test COMMAND arbitration_test)

add_executable(layout_interpolation_test tests/layout_interpolation_test.cpp)
target_link_libraries(layout_interpolation_test PRIVATE motion)
target_compile_options(layout_interpolation_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME layout_interpolation_test COMMAND layout_interpolation_test)

add_executable(implicit_property_test tests/implicit_property_test.cpp)
target_link_libraries(implicit_property_test PRIVATE motion)
target_compile_options(implicit_property_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME implicit_property_test COMMAND implicit_property_test)

add_executable(implicit_retarget_test tests/implicit_retarget_test.cpp)
target_link_libraries(implicit_retarget_test PRIVATE motion)
target_compile_options(implicit_retarget_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME implicit_retarget_test COMMAND implicit_retarget_test)

add_executable(route_transition_test tests/route_transition_test.cpp)
target_link_libraries(route_transition_test PRIVATE motion)
target_compile_options(route_transition_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME route_transition_test COMMAND route_transition_test)

add_executable(implicit_flow_test tests/implicit_flow_test.cpp)
target_link_libraries(implicit_flow_test PRIVATE motion)
target_compile_options(implicit_flow_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME implicit_flow_test COMMAND implicit_flow_test)

add_executable(frame_budget_test tests/frame_budget_test.cpp)
target_link_libraries(frame_budget_test PRIVATE motion)
target_compile_options(frame_budget_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME frame_budget_test COMMAND frame_budget_test)

add_executable(interrupt_session_test tests/interrupt_session_test.cpp)
target_link_libraries(interrupt_session_test PRIVATE motion)
target_compile_options(interrupt_session_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME interrupt_session_test COMMAND interrupt_session_test)

add_executable(security_hardening_test tests/security_hardening_test.cpp)
target_link_libraries(security_hardening_test PRIVATE motion)
target_compile_options(security_hardening_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME security_hardening_test COMMAND security_hardening_test)

add_executable(retarget_test tests/retarget_test.cpp)
target_link_libraries(retarget_test PRIVATE motion)
target_compile_options(retarget_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME retarget_test COMMAND retarget_test)

add_executable(virtual_clock_test tests/virtual_clock_test.cpp)
target_link_libraries(virtual_clock_test PRIVATE motion)
target_compile_options(virtual_clock_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME virtual_clock_test COMMAND virtual_clock_test)

add_executable(replay_determinism_test tests/replay_determinism_test.cpp)
target_link_libraries(replay_determinism_test PRIVATE motion)
target_compile_options(replay_determinism_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME replay_determinism_test COMMAND replay_determinism_test)

add_executable(trace_correlation_test tests/trace_correlation_test.cpp)
target_link_libraries(trace_correlation_test PRIVATE motion)
target_compile_options(trace_correlation_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME trace_correlation_test COMMAND trace_correlation_test)

add_executable(cross_machine_replay_test tests/cross_machine_replay_test.cpp)
target_link_libraries(cross_machine_replay_test PRIVATE motion)
target_compile_options(cross_machine_replay_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME cross_machine_replay_test COMMAND cross_machine_replay_test)

add_executable(asset_control_test tests/asset_control_test.cpp)
target_link_libraries(asset_control_test PRIVATE motion)
target_compile_options(asset_control_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME asset_control_test COMMAND asset_control_test)

add_executable(asset_import_test tests/asset_import_test.cpp)
target_link_libraries(asset_import_test PRIVATE motion)
target_compile_options(asset_import_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME asset_import_test COMMAND asset_import_test)

add_executable(vector_importer_test tests/vector_importer_test.cpp)
target_link_libraries(vector_importer_test PRIVATE motion)
target_compile_options(vector_importer_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME vector_importer_test COMMAND vector_importer_test)

add_executable(record_replay_test tests/record_replay_test.cpp)
target_link_libraries(record_replay_test PRIVATE motion)
target_compile_options(record_replay_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME record_replay_test COMMAND record_replay_test)

add_executable(asset_seek_test tests/asset_seek_test.cpp)
target_link_libraries(asset_seek_test PRIVATE motion)
target_compile_options(asset_seek_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME asset_seek_test COMMAND asset_seek_test)

add_executable(asset_scale_test tests/asset_scale_test.cpp)
target_link_libraries(asset_scale_test PRIVATE motion)
target_compile_options(asset_scale_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME asset_scale_test COMMAND asset_scale_test)

add_executable(asset_prepare_thread_test tests/asset_prepare_thread_test.cpp)
target_link_libraries(asset_prepare_thread_test PRIVATE motion)
target_compile_options(asset_prepare_thread_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME asset_prepare_thread_test COMMAND asset_prepare_thread_test)

add_executable(shared_identity_test tests/shared_identity_test.cpp)
target_link_libraries(shared_identity_test PRIVATE motion)
target_compile_options(shared_identity_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME shared_identity_test COMMAND shared_identity_test)

add_executable(shared_clip_reverse_test tests/shared_clip_reverse_test.cpp)
target_link_libraries(shared_clip_reverse_test PRIVATE motion)
target_compile_options(shared_clip_reverse_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME shared_clip_reverse_test COMMAND shared_clip_reverse_test)

add_executable(shared_pview_test tests/shared_pview_test.cpp)
target_link_libraries(shared_pview_test PRIVATE motion)
target_compile_options(shared_pview_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME shared_pview_test COMMAND shared_pview_test)

add_executable(container_transform_test tests/container_transform_test.cpp)
target_link_libraries(container_transform_test PRIVATE motion)
target_compile_options(container_transform_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME container_transform_test COMMAND container_transform_test)

add_executable(hinge_motion_test tests/hinge_motion_test.cpp)
target_link_libraries(hinge_motion_test PRIVATE motion)
target_compile_options(hinge_motion_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME hinge_motion_test COMMAND hinge_motion_test)

add_executable(size_scaled_motion_test tests/size_scaled_motion_test.cpp)
target_link_libraries(size_scaled_motion_test PRIVATE motion)
target_compile_options(size_scaled_motion_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME size_scaled_motion_test COMMAND size_scaled_motion_test)

add_executable(spring_class_test tests/spring_class_test.cpp)
target_link_libraries(spring_class_test PRIVATE motion)
target_compile_options(spring_class_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME spring_class_test COMMAND spring_class_test)

add_executable(m3_tokens_test tests/m3_tokens_test.cpp)
target_link_libraries(m3_tokens_test PRIVATE motion)
target_compile_options(m3_tokens_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME m3_tokens_test COMMAND m3_tokens_test)

# TC-MOTION-044: file-existence/content assertion path for tokens/m3.json.
# Implemented WITHOUT touching the committed m3_tokens_test.cpp: a hermetic,
# shell-free cmake -P script (generated into the build dir at configure time)
# asserting the file exists and carries a non-empty "version" key.
file(WRITE ${CMAKE_BINARY_DIR}/check_m3_tokens.cmake
  "file(READ \"${CMAKE_CURRENT_SOURCE_DIR}/tokens/m3.json\" _m3_json)\n"
  "if(NOT _m3_json MATCHES \"\\\"version\\\"[ \\t\\r\\n]*:[ \\t\\r\\n]*\\\"[^\\\"]+\\\"\")\n"
  "  message(FATAL_ERROR \"TC-MOTION-044: tokens/m3.json missing or has empty 'version' key\")\n"
  "endif()\n"
  "message(STATUS \"TC-MOTION-044: tokens/m3.json exists with non-empty version key\")\n"
)
add_test(NAME m3_tokens_file COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/check_m3_tokens.cmake)

add_executable(motion_tokens_test tests/motion_tokens_test.cpp)
target_link_libraries(motion_tokens_test PRIVATE motion)
target_compile_options(motion_tokens_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME motion_tokens_test COMMAND motion_tokens_test)

add_executable(implicit_test tests/implicit_test.cpp)
target_link_libraries(implicit_test PRIVATE motion)
target_compile_options(implicit_test PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME implicit_test COMMAND implicit_test)

add_executable(shared_element_test tests/shared_element_test.cpp)
target_link_libraries(shared_element_test PRIVATE motion)
target_compile_options(shared_element_test PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME shared_element_test COMMAND shared_element_test)

add_executable(patterns_test tests/patterns_test.cpp)
target_link_libraries(patterns_test PRIVATE motion)
target_compile_options(patterns_test PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME patterns_test COMMAND patterns_test)

add_executable(reduced_motion_test tests/reduced_motion_test.cpp)
target_link_libraries(reduced_motion_test PRIVATE motion)
target_compile_options(reduced_motion_test PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME reduced_motion_test COMMAND reduced_motion_test)

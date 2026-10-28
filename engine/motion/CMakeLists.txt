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
)
target_include_directories(motion PUBLIC include)
target_compile_options(motion PRIVATE -Wall -Wextra -Wpedantic)

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

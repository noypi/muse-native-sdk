cmake_minimum_required(VERSION 3.16)
project(muse_motion CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(motion
  src/solver.cpp
  src/graph.cpp
  src/gesture_binding.cpp
  src/arbitration.cpp
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

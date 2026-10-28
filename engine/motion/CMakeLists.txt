cmake_minimum_required(VERSION 3.16)
project(muse_motion CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(motion
  src/solver.cpp
  src/graph.cpp
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

cmake_minimum_required(VERSION 3.16)
project(muse_layout CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(layout
  src/types.cpp
  src/solver.cpp
)
target_include_directories(layout PUBLIC include)
target_compile_options(layout PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(smoke_test tests/smoke_test.cpp)
target_link_libraries(smoke_test PRIVATE layout)
target_compile_options(smoke_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

include(CTest)
enable_testing()
add_test(NAME smoke_test COMMAND smoke_test)

add_executable(solver_test tests/solver_test.cpp)
target_link_libraries(solver_test PRIVATE layout)
target_compile_options(solver_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME solver_test COMMAND solver_test)

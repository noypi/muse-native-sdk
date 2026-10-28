cmake_minimum_required(VERSION 3.16)
project(muse_layout CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(layout
  src/types.cpp
  src/solver.cpp
  src/delegate.cpp
  src/sliver.cpp
  src/masonry.cpp
  src/form_factor.cpp
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

add_executable(delegate_test tests/delegate_test.cpp)
target_link_libraries(delegate_test PRIVATE layout)
target_compile_options(delegate_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME delegate_test COMMAND delegate_test)

add_executable(geometry_test tests/geometry_test.cpp)
target_link_libraries(geometry_test PRIVATE layout)
target_compile_options(geometry_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME geometry_test COMMAND geometry_test)

add_executable(form_factor_test tests/form_factor_test.cpp)
target_link_libraries(form_factor_test PRIVATE layout)
target_compile_options(form_factor_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME form_factor_test COMMAND form_factor_test)

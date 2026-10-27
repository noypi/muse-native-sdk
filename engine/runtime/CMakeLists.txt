cmake_minimum_required(VERSION 3.16)
project(muse_runtime CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(runtime
  src/tree.cpp
  src/frame.cpp
  src/layout.cpp
)
target_include_directories(runtime PUBLIC include)
target_compile_options(runtime PRIVATE -Wall -Wextra -Wpedantic)

add_executable(instantiate_test tests/instantiate_test.cpp)
target_compile_options(instantiate_test PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
enable_testing()
add_test(NAME instantiate_test COMMAND instantiate_test)

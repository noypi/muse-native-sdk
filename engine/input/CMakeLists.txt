cmake_minimum_required(VERSION 3.16)
project(muse_input CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(input STATIC src/event_model.cpp)
target_include_directories(input PUBLIC include)
target_compile_options(input PRIVATE -Wall -Wextra -Wpedantic)

add_executable(event_model_test tests/event_model_test.cpp)
target_include_directories(event_model_test PRIVATE include)
target_link_libraries(event_model_test PRIVATE input)
target_compile_options(event_model_test PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
enable_testing()
add_test(NAME event_model_test COMMAND event_model_test)

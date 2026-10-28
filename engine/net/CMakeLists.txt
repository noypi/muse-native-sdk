cmake_minimum_required(VERSION 3.16)
project(muse_net CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(net
  src/data_layer.cc
  src/sqlite_store.cc
)
target_include_directories(net PUBLIC include)
target_compile_options(net PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
enable_testing()

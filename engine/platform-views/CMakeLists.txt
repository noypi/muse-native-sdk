cmake_minimum_required(VERSION 3.16)
project(muse_pviews CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Platform-views native sources (NFR-PVIEWS-001 / FR-PVIEWS-001).
add_library(pviews
  src/external_memory_probe.cpp
  src/z_order.cpp
)
target_include_directories(pviews PUBLIC include)
target_compile_options(pviews PRIVATE -Wall -Wextra -Wpedantic)

# Brownfield embed target (FR-PVIEWS-003 / AC-PVIEWS-004).
add_library(pviews-embed
  src/embed.cpp
)
target_include_directories(pviews-embed PUBLIC include)
target_compile_options(pviews-embed PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
enable_testing()

add_executable(external_memory_test tests/external_memory_test.cpp)
target_link_libraries(external_memory_test PRIVATE pviews)
target_compile_options(external_memory_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME external_memory COMMAND external_memory_test)

add_executable(z_order_test tests/z_order_test.cpp)
target_link_libraries(z_order_test PRIVATE pviews)
target_compile_options(z_order_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME z_order COMMAND z_order_test)

add_executable(brownfield_embed_test tests/brownfield_embed_test.cpp)
target_link_libraries(brownfield_embed_test PRIVATE pviews-embed)
target_compile_options(brownfield_embed_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME brownfield_embed COMMAND brownfield_embed_test)
